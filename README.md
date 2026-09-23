# Custom-MPH-HUD

This repository is intentionally minimal.

The ESP32 is the HUD. Once flashed, it boots and runs by itself every time it receives power. It does not use Wi-Fi and does not load a webpage at runtime.

For now the phone sends live driving data to the ESP32 over Bluetooth Low Energy:

`current_speed_mph,speed_limit_mph`

Example:

`47.2,45`

The ESP32 handles the display, color logic, turn-signal edge lighting, and ambient-light dimming locally.

## Display

Target display: 2.0-inch 240x320 ST7789 SPI TFT.

The screen contains only one large mirrored speed number on a black background.

Color logic:

- 10+ MPH over the speed limit: red
- 5 to 10 MPH over: fade orange toward red
- within 5 MPH of the limit: orange
- 5 MPH under: blue
- 5 to 10 MPH under: fade blue toward purple
- 10+ MPH under: purple
- if no speed limit has been supplied yet: white

## Turn signals

- left signal input active: only the left edge lights green
- right signal input active: only the right edge lights green
- hazards: both edges light green

The ESP32 follows the truck's real blinker pulse. It does not generate a fake blink timing.

GPIO 32 = left turn input
GPIO 33 = right turn input

Vehicle lighting wiring must be conditioned down to safe 3.3 V logic before reaching the ESP32.

## Ambient light

GPIO 34 reads an LDR/photoresistor voltage divider.

The brightness is reduced automatically in darkness and increased in daylight.

GPIO 25 is reserved for the final TFT backlight driver. Do not connect the TFT backlight directly to GPIO 25 until the backlight-current circuit is confirmed.

## TFT wiring

| TFT | ESP32 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO 18 |
| SDA | GPIO 23 |
| RST | GPIO 17 |
| DC | GPIO 16 |
| CS | GPIO 5 |
| BL | powered normally until the backlight driver is added |

## Firmware

The only program that belongs on the ESP32 is:

`firmware/Custom_MPH_HUD.ino`

No Wi-Fi code is used.
