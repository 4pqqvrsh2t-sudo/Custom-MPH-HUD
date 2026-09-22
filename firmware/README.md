# ESP32 wiring

## ST7789 display

The pictured module is the 2.0-inch 240x320 ST7789 SPI display with these eight pins.

| TFT pin | ESP32 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO 18 |
| SDA | GPIO 23 |
| RST | GPIO 17 |
| DC | GPIO 16 |
| CS | GPIO 5 |
| BL | Power normally for the first bench test |

The firmware uses landscape mode, so the display becomes 320x240.

## Ambient light sensor

Use one photoresistor and one 10 kΩ resistor as a voltage divider.

Basic arrangement:

```
3.3V
 |
 LDR
 |
 +------ GPIO 34
 |
10k resistor
 |
GND
```

With that arrangement, GPIO 34 reads a larger number in brighter light.

The default calibration values in the sketch are only starting points. Once the hardware is together, look at the Serial Monitor in bright daylight and in darkness and set `LIGHT_DARK_RAW` and `LIGHT_BRIGHT_RAW` to match the real readings.

## Backlight control

GPIO 25 is reserved as the automatic backlight PWM command.

Do not connect the TFT backlight to GPIO 25 unless the display's backlight current/control input has been verified. The safe final arrangement is for GPIO 25 to control a transistor/MOSFET backlight driver.

Even before that driver is installed, the firmware still dims the drawn speed digits and green turn-signal edges according to the light sensor.

## Turn-signal inputs

- GPIO 32 = left signal input.
- GPIO 33 = right signal input.

The ESP32 expects 3.3 V logic only.

Do not connect the truck's 12 V turn-signal wires directly to these pins. Use an isolation/input-conditioning circuit first.

## Arduino IDE

Install:

- ESP32 board support from Espressif.
- Adafruit GFX Library.
- Adafruit ST7735 and ST7789 Library.

Open `Custom_MPH_HUD.ino`, select the ESP32 board and USB port, then Upload.
