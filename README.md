# xLicht 🎮 ✨
### The Ultimate Smart LED Fan Mod for Xbox 360 RGH

**xLicht** connects your Xbox 360’s internal fan to a smart WS2812B LED ring that reacts to the game you are playing in real-time.

Unlike legacy mods that require compiling complex `.xex` plugins, xLicht uses a **UART Sniffing** technique. It listens to the debug logs natively output by the console and detects when **Aurora Dashboard** launches a game.

## ✨ Features
*   **Game Awareness:** Automatically switches colors based on the active game (e.g., Halo 3 = Blue, Gears of War = Red).
*   **Web Dashboard:** Configure everything from your phone via a WiFi Captive Portal. No app required.
*   **Game Library:** Save, load, and delete custom color profiles for up to 200 games.
*   **Boot Animation:** Smooth, 5-second startup sequence (Wipe + Breathing).
*   **Safe Power:** Built-in software current limiting (850mA) allows safe powering directly from the motherboard.

## ❓ Why Aurora Dashboard?
xLicht works as a "Passive Listener". It does not run any code on the Xbox itself. Instead, it relies on the debug output that **Aurora Dashboard** generates when it launches a game or returns to the menu.
*   When you launch a game, Aurora prints: `Launcher Path: ...` or `TitleId changed ...`
*   xLicht reads this text via the hardware UART lines, identifies the game ID, and updates the LEDs.
*   **Requirement:** Aurora must be your default dashboard for this detection to work reliably.

## 🛠 Hardware Required
| Component | Recommendation |
| :--- | :--- |
| **Console** | Xbox 360 Slim (Trinity or Corona motherboard) with RGH/JTAG. |
| **MCU** | **ESP32 DevKit V1** (Dual Core required for smooth animations). |
| **LEDs** | **WS2812B Strip** (144 LEDs/m, IP30 Black PCB). Cut to fit fan size. |
| **Wiring** | 30 AWG (Data), 22 AWG (Power). |
| **Power** | 5V Motherboard Tap (Capacitor spot) OR 12V Main Rail + UBEC 5V 3A. |

## 🔌 Wiring Guide
### 1. Data Connection (The "Sniffer")
You need to tap into the UART (Post out / EX) on the motherboard.
*   **Pin 1 (Xbox TX):** Connect to ESP32 **GPIO 16** (RX2).
*   **Pin 2 (Ground):** Connect to ESP32 **GND**.

### 2. LED Connection
*   **Data:** Connect LED Green Wire to ESP32 **D2 (GPIO 4)** - *Note: Code default is GPIO 5, please check define `LED_PIN` in code*.
*   **Power:** Connect 5V and ground to the strip (ensure common ground with ESP32).

### 3. Power Source
*   **Option A (Easiest):** Tap a **5V Switched** point (like the DVD drive power connector or a nearby 5V capacitor pad).
    *   *Note:* The firmware is set to limit power to **850mA** for safety.
*   **Option B (Pro):** Tap the main **12V Rail** (under PSU socket) and use a **UBEC 5V 3A** to step it down.

## 💾 Installation & Setup

### 1. Console Preparation
1.  Boot your Xbox and open **DashLaunch**.
2.  Go to **Configurator > Settings**.
3.  Ensure `debugout` is set to **TRUE**.
4.  Save the settings to your `launch.ini`.
5.  **Aurora Dashboard** must be your default dashboard.

### 2. Flashing the ESP32
1.  Open the project in **Arduino IDE**.
2.  **Security Setup (IMPORTANT):** Edit the `CONFIGURATION` section in the `xLicht_ESP.ino` file to set your WiFi credentials and passwords.
    ```cpp
    const char* ssid = "YOUR_WIFI_SSID";
    const char* password = "YOUR_WIFI_PASS";
    const char* app_pass = "admin"; // Web App Password
    ```
3.  Install libraries via Library Manager:
    *   **FastLED**
    *   **ArduinoJson**
    *   **ESPAsyncWebServer**
    *   **AsyncTCP**
4.  Select Board: **DOIT ESP32 DEVKIT V1** and upload.

### 3. Using the App
1.  Connect your phone to WiFi: **xLicht** (or your home WiFi IP).
2.  The configuration page should open automatically (captive portal) or go to the IP address printed in Serial Monitor.
3.  Login with your **Web App Password**.
4.  **Configure:** Set your "Active LEDs" and default system color.
5.  **Play:** Launch a game! The App will show the Game ID; name it and save a color.

## 🐍 Troubleshooting
*   **"Waiting..." status never changes:**
    *   Verify your wire is on **J2B1 Pin 2**.
    *   Verify `debugout = true` is saved in DashLaunch.
    *   Ensure **Aurora Dashboard** is running.
*   **LEDs flash white then turn red on boot:**
    *   This is part of the ESP32 self-test process. The code starts then reboots during startup.

## 🧡 Credits
*   **FastLED** for animations.
*   **ArduinoJson** for settings.
*   Inspired by the Xbox 360 RGH modding community.

## 📄 License
This project is licensed under the MIT License.
