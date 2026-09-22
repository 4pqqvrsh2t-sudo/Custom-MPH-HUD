# Custom-MPH-HUD

Fresh start: this project currently does only three things.

1. Gets live vehicle speed from the iPhone GPS.
2. Sends that speed over Bluetooth Low Energy to an ESP32, which draws it on the 2.0-inch ST7789 display.
3. Shows physical turn signals as thin green strips on the matching screen edge and automatically dims the HUD from an ambient-light sensor.

No speed-limit database or navigation logic is included yet.

## Hardware currently targeted

- AITRIP ESP32 ESP-WROOM-32, 30-pin, USB-C.
- GODIYMODULES 2.0-inch IPS TFT, 240x320, ST7789, SPI, 8 pins:
  GND, VCC, SCL, SDA, RST, DC, CS, BL.
- One photoresistor (LDR) plus one 10 kΩ resistor for ambient light.
- Two safely conditioned turn-signal inputs.

## Repository layout

- `firmware/Custom_MPH_HUD.ino` — code that gets flashed onto the ESP32.
- `index.html`, `app.js`, `styles.css` — the iPhone page that reads GPS speed and sends it to the ESP32.

## What appears on the HUD

The TFT is landscape with a black background. The speed is large and centered. The digits are pre-mirrored so they read normally in the windshield reflection.

When the left turn signal is active, only a thin strip on the left edge turns green. When the right signal is active, only the right edge turns green. Hazards light both edges.

If the phone stops sending data for 4 seconds, the ESP32 replaces the speed with gray dashes instead of leaving an old speed displayed.

## Ambient-light dimming

The ESP32 reads an LDR on GPIO 34. The firmware always scales the brightness of the speed digits and turn-signal strips using that reading.

The firmware also has a PWM output on GPIO 25 for a proper backlight driver. Do not assume the TFT's BL pin can safely be powered directly from an ESP32 GPIO. For first testing, power BL normally. When we add the transistor/MOSFET backlight driver, GPIO 25 will control it automatically.

## Important vehicle-input rule

Do not connect a 12 V turn-signal wire directly to the ESP32.

GPIO 32 and GPIO 33 must only receive safe 3.3 V logic from an isolation/input-conditioning circuit. We can build that part once the display/GPS side is working.

## Flashing the ESP32

Use Arduino IDE:

1. Install the ESP32 board package.
2. Install **Adafruit GFX Library**.
3. Install **Adafruit ST7735 and ST7789 Library**.
4. Open `firmware/Custom_MPH_HUD.ino`.
5. Select an ESP32 Dev Module/ESP32 WROOM board and the correct USB port.
6. Click Upload.

The ESP32 will advertise itself as **Custom MPH HUD**.

## iPhone page

The phone page uses the iPhone GPS and Web Bluetooth. Safari on iPhone does not expose Web Bluetooth, so use a browser that does (for example Bluefy) when pairing to the ESP32.

To host the page, enable GitHub Pages for the repository using the `main` branch and repository root.

Then:

1. Power the ESP32.
2. Open the GitHub Pages URL on the iPhone.
3. Tap **Connect HUD** and select **Custom MPH HUD**.
4. Tap **Start GPS** and allow precise location.
5. The phone begins sending MPH to the ESP32.
