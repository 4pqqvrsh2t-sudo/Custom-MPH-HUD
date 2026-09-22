#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

#define LEFT_TURN_PIN  32
#define RIGHT_TURN_PIN 33

#define LIGHT_SENSOR_PIN 34
#define BACKLIGHT_PWM_PIN 25

#define EDGE_WIDTH 6
#define DATA_TIMEOUT_MS 4000
#define HUD_MIRROR true

// Starting calibration values for the LDR divider.
// Adjust these after looking at real analogRead() values in darkness and daylight.
#define LIGHT_DARK_RAW   250
#define LIGHT_BRIGHT_RAW 3500

// 0-255 range. Keep a little brightness at night so the HUD remains readable.
#define MIN_VISUAL_BRIGHTNESS 28
#define MAX_VISUAL_BRIGHTNESS 255

// GPIO 25 is intended to drive a transistor/MOSFET backlight-control circuit.
// Leave false until that driver is physically installed.
#define ENABLE_BACKLIGHT_PWM false

static const char* SERVICE_UUID="c6f51001-46bb-4bb5-a8dd-000000000001";
static const char* SPEED_UUID  ="c6f51002-46bb-4bb5-a8dd-000000000001";

Adafruit_ST7789 tft(TFT_CS,TFT_DC,TFT_RST);

volatile float incomingMph=0.0f;
volatile uint32_t lastPacketMs=0;
portMUX_TYPE speedMux=portMUX_INITIALIZER_UNLOCKED;

float filteredLight=0;
uint8_t visualBrightness=180;

int lastSpeed=-999;
bool lastTimedOut=true;
bool lastLeft=false;
bool lastRight=false;

enum Segment:uint8_t{
  SEG_A=1<<0,
  SEG_B=1<<1,
  SEG_C=1<<2,
  SEG_D=1<<3,
  SEG_E=1<<4,
  SEG_F=1<<5,
  SEG_G=1<<6
};

const uint8_t DIGITS[10]={
  SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F,
  SEG_B|SEG_C,
  SEG_A|SEG_B|SEG_G|SEG_E|SEG_D,
  SEG_A|SEG_B|SEG_C|SEG_D|SEG_G,
  SEG_F|SEG_G|SEG_B|SEG_C,
  SEG_A|SEG_F|SEG_G|SEG_C|SEG_D,
  SEG_A|SEG_F|SEG_G|SEG_E|SEG_C|SEG_D,
  SEG_A|SEG_B|SEG_C,
  SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,
  SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G
};

uint8_t scale8(uint8_t value){
  return (uint16_t)value*visualBrightness/255;
}

uint16_t dimmedColor(uint8_t r,uint8_t g,uint8_t b){
  return tft.color565(scale8(r),scale8(g),scale8(b));
}

uint8_t mirroredSegments(uint8_t s){
  uint8_t out=0;

  if(s&SEG_A)out|=SEG_A;
  if(s&SEG_D)out|=SEG_D;
  if(s&SEG_G)out|=SEG_G;

  if(s&SEG_B)out|=SEG_F;
  if(s&SEG_F)out|=SEG_B;
  if(s&SEG_C)out|=SEG_E;
  if(s&SEG_E)out|=SEG_C;

  return out;
}

void drawDigit(int x,int y,int w,int h,int thick,int digit,uint16_t color,bool mirror){
  if(digit<0||digit>9)return;

  uint8_t s=DIGITS[digit];
  if(mirror)s=mirroredSegments(s);

  const int half=h/2;

  if(s&SEG_A)tft.fillRoundRect(x+thick,y,w-2*thick,thick,thick/2,color);
  if(s&SEG_G)tft.fillRoundRect(x+thick,y+half-thick/2,w-2*thick,thick,thick/2,color);
  if(s&SEG_D)tft.fillRoundRect(x+thick,y+h-thick,w-2*thick,thick,thick/2,color);

  if(s&SEG_F)tft.fillRoundRect(x,y+thick,thick,half-thick,thick/2,color);
  if(s&SEG_B)tft.fillRoundRect(x+w-thick,y+thick,thick,half-thick,thick/2,color);
  if(s&SEG_E)tft.fillRoundRect(x,y+half,thick,half-thick,thick/2,color);
  if(s&SEG_C)tft.fillRoundRect(x+w-thick,y+half,thick,half-thick,thick/2,color);
}

void clearCenter(){
  tft.fillRect(EDGE_WIDTH,0,tft.width()-EDGE_WIDTH*2,tft.height(),ST77XX_BLACK);
}

void drawDisconnected(){
  clearCenter();

  const int y=tft.height()/2-5;
  const int dashW=54;
  const int gap=18;
  const int startX=(tft.width()-(dashW*2+gap))/2;
  const uint16_t gray=dimmedColor(95,95,95);

  tft.fillRoundRect(startX,y,dashW,10,4,gray);
  tft.fillRoundRect(startX+dashW+gap,y,dashW,10,4,gray);

  lastSpeed=-999;
}

void drawSpeed(int mph){
  mph=constrain(mph,0,180);

  String s=String(mph);

  const int count=s.length();
  const int digitW=count==3?72:84;
  const int digitH=158;
  const int thick=13;
  const int gap=10;

  const int totalW=count*digitW+(count-1)*gap;
  const int startX=(tft.width()-totalW)/2;
  const int y=(tft.height()-digitH)/2;

  const uint16_t white=dimmedColor(255,255,255);

  for(int i=0;i<count;i++){
    const int sourceIndex=HUD_MIRROR?(count-1-i):i;
    const int digit=s[sourceIndex]-'0';

    drawDigit(
      startX+i*(digitW+gap),
      y,
      digitW,
      digitH,
      thick,
      digit,
      white,
      HUD_MIRROR
    );
  }
}

void updateAmbientBrightness(){
  static uint32_t lastRead=0;
  const uint32_t now=millis();

  if(now-lastRead<80)return;
  lastRead=now;

  const int raw=analogRead(LIGHT_SENSOR_PIN);

  if(filteredLight==0)filteredLight=raw;
  filteredLight=filteredLight*0.88f+raw*0.12f;

  float t=(filteredLight-LIGHT_DARK_RAW)/(float)(LIGHT_BRIGHT_RAW-LIGHT_DARK_RAW);
  t=constrain(t,0.0f,1.0f);

  // Slight curve keeps night brightness low while still allowing strong daylight output.
  t=t*t;

  visualBrightness=(uint8_t)(
    MIN_VISUAL_BRIGHTNESS+
    t*(MAX_VISUAL_BRIGHTNESS-MIN_VISUAL_BRIGHTNESS)
  );

  if(ENABLE_BACKLIGHT_PWM){
    analogWrite(BACKLIGHT_PWM_PIN,visualBrightness);
  }

  Serial.print("Light raw: ");
  Serial.print(raw);
  Serial.print("  filtered: ");
  Serial.print((int)filteredLight);
  Serial.print("  brightness: ");
  Serial.println(visualBrightness);
}

void updateTurnEdges(){
  const bool left=digitalRead(LEFT_TURN_PIN)==HIGH;
  const bool right=digitalRead(RIGHT_TURN_PIN)==HIGH;
  const uint16_t green=dimmedColor(0,255,65);

  // Redraw every cycle when active so ambient-light changes also change edge brightness.
  if(left){
    tft.fillRect(0,0,EDGE_WIDTH,tft.height(),green);
  }else if(lastLeft){
    tft.fillRect(0,0,EDGE_WIDTH,tft.height(),ST77XX_BLACK);
  }

  if(right){
    tft.fillRect(tft.width()-EDGE_WIDTH,0,EDGE_WIDTH,tft.height(),green);
  }else if(lastRight){
    tft.fillRect(tft.width()-EDGE_WIDTH,0,EDGE_WIDTH,tft.height(),ST77XX_BLACK);
  }

  lastLeft=left;
  lastRight=right;
}

class ServerCallbacks:public BLEServerCallbacks{
  void onDisconnect(BLEServer* server) override{
    BLEDevice::startAdvertising();
  }
};

class SpeedCallbacks:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* characteristic) override{
    String message(characteristic->getValue().c_str());
    message.trim();

    const float mph=message.toFloat();

    if(!isfinite(mph)||mph<0.0f||mph>180.0f)return;

    portENTER_CRITICAL(&speedMux);
    incomingMph=mph;
    lastPacketMs=millis();
    portEXIT_CRITICAL(&speedMux);
  }
};

void setupBle(){
  BLEDevice::init("Custom MPH HUD");

  BLEServer* server=BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service=server->createService(SERVICE_UUID);

  BLECharacteristic* speed=service->createCharacteristic(
    SPEED_UUID,
    BLECharacteristic::PROPERTY_WRITE|
    BLECharacteristic::PROPERTY_WRITE_NR
  );

  speed->setCallbacks(new SpeedCallbacks());
  service->start();

  BLEAdvertising* advertising=BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void setup(){
  Serial.begin(115200);

  pinMode(LEFT_TURN_PIN,INPUT_PULLDOWN);
  pinMode(RIGHT_TURN_PIN,INPUT_PULLDOWN);
  pinMode(LIGHT_SENSOR_PIN,INPUT);
  pinMode(BACKLIGHT_PWM_PIN,OUTPUT);

  if(ENABLE_BACKLIGHT_PWM){
    analogWrite(BACKLIGHT_PWM_PIN,visualBrightness);
  }else{
    analogWrite(BACKLIGHT_PWM_PIN,0);
  }

  SPI.begin(TFT_SCLK,-1,TFT_MOSI,TFT_CS);

  tft.init(240,320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  updateAmbientBrightness();
  drawDisconnected();
  updateTurnEdges();

  setupBle();
}

void loop(){
  updateAmbientBrightness();
  updateTurnEdges();

  float mph;
  uint32_t packetTime;

  portENTER_CRITICAL(&speedMux);
  mph=incomingMph;
  packetTime=lastPacketMs;
  portEXIT_CRITICAL(&speedMux);

  const bool timedOut=packetTime==0||(millis()-packetTime>DATA_TIMEOUT_MS);

  if(timedOut){
    if(!lastTimedOut){
      drawDisconnected();
    }
    lastTimedOut=true;
    delay(20);
    return;
  }

  lastTimedOut=false;

  const int rounded=(int)lroundf(mph);

  // Redraw continuously so automatic dimming updates even if speed does not change.
  clearCenter();
  drawSpeed(rounded);
  lastSpeed=rounded;

  delay(25);
}
