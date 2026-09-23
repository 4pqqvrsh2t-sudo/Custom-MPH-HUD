#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// -----------------------------
// Hardware
// -----------------------------
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

#define LEFT_TURN_PIN   32
#define RIGHT_TURN_PIN  33
#define LIGHT_SENSOR_PIN 34
#define BACKLIGHT_PWM_PIN 25

#define EDGE_WIDTH 6
#define DATA_TIMEOUT_MS 4000
#define HUD_MIRROR true

// Starting values only. Calibrate after the LDR is physically installed.
#define LIGHT_DARK_RAW   250
#define LIGHT_BRIGHT_RAW 3500

#define MIN_BRIGHTNESS 24
#define MAX_BRIGHTNESS 255

// Leave false until GPIO 25 drives a proper transistor/MOSFET
// connected to the TFT backlight circuit.
#define ENABLE_BACKLIGHT_PWM false

// -----------------------------
// BLE
// -----------------------------
static const char* SERVICE_UUID =
  "c6f51001-46bb-4bb5-a8dd-000000000001";

static const char* DATA_UUID =
  "c6f51002-46bb-4bb5-a8dd-000000000001";

// Phone packet:
// currentSpeedMph,speedLimitMph
//
// Example:
// 47.2,45
//
// Use -1 for speedLimitMph when unknown.

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

volatile float currentSpeedMph = 0.0f;
volatile int currentLimitMph = -1;
volatile uint32_t lastPacketMs = 0;

portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

// -----------------------------
// Color system
// -----------------------------
struct RGB {
  float r;
  float g;
  float b;
};

const RGB RED    = {255,  0,   0};
const RGB ORANGE = {255, 132,  0};
const RGB BLUE   = {  0, 135,255};
const RGB PURPLE = {170,  0, 255};
const RGB WHITE  = {255,255, 255};

RGB shownColor = WHITE;

RGB mixColor(const RGB& a, const RGB& b, float t) {
  t = constrain(t, 0.0f, 1.0f);

  return {
    a.r + (b.r - a.r) * t,
    a.g + (b.g - a.g) * t,
    a.b + (b.b - a.b) * t
  };
}

RGB targetColor(float speed, int limit) {
  if (limit <= 0) return WHITE;

  const float diff = speed - limit;

  if (diff >= 10.0f) return RED;

  if (diff > 5.0f) {
    return mixColor(ORANGE, RED, (diff - 5.0f) / 5.0f);
  }

  if (diff > -5.0f) return ORANGE;

  if (diff > -10.0f) {
    return mixColor(BLUE, PURPLE, (-diff - 5.0f) / 5.0f);
  }

  return PURPLE;
}

// -----------------------------
// Ambient brightness
// -----------------------------
float filteredLight = 0.0f;
uint8_t visualBrightness = 180;

uint8_t dimChannel(float value) {
  value = constrain(value, 0.0f, 255.0f);
  return (uint16_t)value * visualBrightness / 255;
}

uint16_t hudColor(const RGB& color) {
  return tft.color565(
    dimChannel(color.r),
    dimChannel(color.g),
    dimChannel(color.b)
  );
}

void updateBrightness() {
  static uint32_t lastRead = 0;

  if (millis() - lastRead < 80) return;
  lastRead = millis();

  const int raw = analogRead(LIGHT_SENSOR_PIN);

  if (filteredLight == 0.0f) filteredLight = raw;

  filteredLight =
    filteredLight * 0.88f +
    raw * 0.12f;

  float level =
    (filteredLight - LIGHT_DARK_RAW) /
    (float)(LIGHT_BRIGHT_RAW - LIGHT_DARK_RAW);

  level = constrain(level, 0.0f, 1.0f);

  // Keeps night output subdued while still allowing full daylight brightness.
  level *= level;

  visualBrightness =
    MIN_BRIGHTNESS +
    level * (MAX_BRIGHTNESS - MIN_BRIGHTNESS);

  if (ENABLE_BACKLIGHT_PWM) {
    analogWrite(BACKLIGHT_PWM_PIN, visualBrightness);
  }
}

// -----------------------------
// Large mirrored digits
// -----------------------------
enum Segment : uint8_t {
  SEG_A = 1 << 0,
  SEG_B = 1 << 1,
  SEG_C = 1 << 2,
  SEG_D = 1 << 3,
  SEG_E = 1 << 4,
  SEG_F = 1 << 5,
  SEG_G = 1 << 6
};

const uint8_t DIGITS[10] = {
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

uint8_t mirrorSegments(uint8_t s) {
  uint8_t out = 0;

  if (s & SEG_A) out |= SEG_A;
  if (s & SEG_D) out |= SEG_D;
  if (s & SEG_G) out |= SEG_G;

  if (s & SEG_B) out |= SEG_F;
  if (s & SEG_F) out |= SEG_B;
  if (s & SEG_C) out |= SEG_E;
  if (s & SEG_E) out |= SEG_C;

  return out;
}

void drawDigit(
  int x,
  int y,
  int w,
  int h,
  int thick,
  int digit,
  uint16_t color,
  bool mirrored
) {
  if (digit < 0 || digit > 9) return;

  uint8_t s = DIGITS[digit];

  if (mirrored) {
    s = mirrorSegments(s);
  }

  const int half = h / 2;

  if (s & SEG_A)
    tft.fillRoundRect(x + thick, y, w - 2*thick, thick, thick/2, color);

  if (s & SEG_G)
    tft.fillRoundRect(x + thick, y + half - thick/2, w - 2*thick, thick, thick/2, color);

  if (s & SEG_D)
    tft.fillRoundRect(x + thick, y + h - thick, w - 2*thick, thick, thick/2, color);

  if (s & SEG_F)
    tft.fillRoundRect(x, y + thick, thick, half - thick, thick/2, color);

  if (s & SEG_B)
    tft.fillRoundRect(x + w - thick, y + thick, thick, half - thick, thick/2, color);

  if (s & SEG_E)
    tft.fillRoundRect(x, y + half, thick, half - thick, thick/2, color);

  if (s & SEG_C)
    tft.fillRoundRect(x + w - thick, y + half, thick, half - thick, thick/2, color);
}

void clearNumberArea() {
  tft.fillRect(
    EDGE_WIDTH,
    0,
    tft.width() - EDGE_WIDTH * 2,
    tft.height(),
    ST77XX_BLACK
  );
}

void drawSpeed(int mph, uint16_t color) {
  mph = constrain(mph, 0, 180);

  String value = String(mph);

  const int count = value.length();
  const int digitW = count == 3 ? 72 : 86;
  const int digitH = 166;
  const int thick = 14;
  const int gap = 10;

  const int totalW =
    count * digitW +
    (count - 1) * gap;

  const int startX =
    (tft.width() - totalW) / 2;

  const int y =
    (tft.height() - digitH) / 2;

  for (int i = 0; i < count; i++) {
    const int sourceIndex =
      HUD_MIRROR
      ? count - 1 - i
      : i;

    const int digit =
      value[sourceIndex] - '0';

    drawDigit(
      startX + i * (digitW + gap),
      y,
      digitW,
      digitH,
      thick,
      digit,
      color,
      HUD_MIRROR
    );
  }
}

void drawNoData() {
  clearNumberArea();

  const uint16_t gray =
    tft.color565(
      visualBrightness / 3,
      visualBrightness / 3,
      visualBrightness / 3
    );

  const int y = tft.height() / 2 - 5;
  const int dashW = 56;
  const int gap = 18;

  const int x =
    (tft.width() - (dashW * 2 + gap)) / 2;

  tft.fillRoundRect(x, y, dashW, 10, 4, gray);
  tft.fillRoundRect(x + dashW + gap, y, dashW, 10, 4, gray);
}

// -----------------------------
// Turn-signal edge strips
// -----------------------------
void drawTurnEdges() {
  const bool left =
    digitalRead(LEFT_TURN_PIN) == HIGH;

  const bool right =
    digitalRead(RIGHT_TURN_PIN) == HIGH;

  const uint16_t green =
    tft.color565(
      0,
      visualBrightness,
      visualBrightness / 4
    );

  tft.fillRect(
    0,
    0,
    EDGE_WIDTH,
    tft.height(),
    left ? green : ST77XX_BLACK
  );

  tft.fillRect(
    tft.width() - EDGE_WIDTH,
    0,
    EDGE_WIDTH,
    tft.height(),
    right ? green : ST77XX_BLACK
  );
}

// -----------------------------
// BLE input
// -----------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onDisconnect(BLEServer* server) override {
    BLEDevice::startAdvertising();
  }
};

class DataCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String msg(characteristic->getValue().c_str());

    msg.trim();

    const int comma = msg.indexOf(',');

    if (comma < 1) return;

    const float speed =
      msg.substring(0, comma).toFloat();

    const int limit =
      msg.substring(comma + 1).toInt();

    if (!isfinite(speed)) return;
    if (speed < 0.0f || speed > 180.0f) return;

    if (
      limit != -1 &&
      (limit < 5 || limit > 90)
    ) {
      return;
    }

    portENTER_CRITICAL(&dataMux);

    currentSpeedMph = speed;
    currentLimitMph = limit;
    lastPacketMs = millis();

    portEXIT_CRITICAL(&dataMux);
  }
};

void setupBle() {
  BLEDevice::init("Custom MPH HUD");

  BLEServer* server =
    BLEDevice::createServer();

  server->setCallbacks(
    new ServerCallbacks()
  );

  BLEService* service =
    server->createService(
      SERVICE_UUID
    );

  BLECharacteristic* data =
    service->createCharacteristic(
      DATA_UUID,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR
    );

  data->setCallbacks(
    new DataCallbacks()
  );

  service->start();

  BLEAdvertising* advertising =
    BLEDevice::getAdvertising();

  advertising->addServiceUUID(
    SERVICE_UUID
  );

  advertising->setScanResponse(true);

  BLEDevice::startAdvertising();
}

// -----------------------------
// Startup
// -----------------------------
void setup() {
  pinMode(
    LEFT_TURN_PIN,
    INPUT_PULLDOWN
  );

  pinMode(
    RIGHT_TURN_PIN,
    INPUT_PULLDOWN
  );

  pinMode(
    LIGHT_SENSOR_PIN,
    INPUT
  );

  pinMode(
    BACKLIGHT_PWM_PIN,
    OUTPUT
  );

  SPI.begin(
    TFT_SCLK,
    -1,
    TFT_MOSI,
    TFT_CS
  );

  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  updateBrightness();
  drawNoData();
  drawTurnEdges();

  setupBle();
}

// -----------------------------
// Main loop
// -----------------------------
void loop() {
  updateBrightness();
  drawTurnEdges();

  float speed;
  int limit;
  uint32_t packetTime;

  portENTER_CRITICAL(&dataMux);

  speed = currentSpeedMph;
  limit = currentLimitMph;
  packetTime = lastPacketMs;

  portEXIT_CRITICAL(&dataMux);

  const bool stale =
    packetTime == 0 ||
    millis() - packetTime > DATA_TIMEOUT_MS;

  if (stale) {
    drawNoData();
    delay(30);
    return;
  }

  RGB target =
    targetColor(speed, limit);

  const float fade = 0.10f;

  shownColor.r +=
    (target.r - shownColor.r) * fade;

  shownColor.g +=
    (target.g - shownColor.g) * fade;

  shownColor.b +=
    (target.b - shownColor.b) * fade;

  clearNumberArea();

  drawSpeed(
    (int)lroundf(speed),
    hudColor(shownColor)
  );

  delay(30);
}
