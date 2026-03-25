# IoT Smart Weather & IR Hub 🌦️📡

A professional-grade desktop companion built on the **ESP8266**. This project features a smart context-switching logic between network-heavy tasks and timing-critical hardware tasks.

## 🛠️ How it Works
* **Weather Mode:** Connects to WiFi, syncs time via NTP, and fetches live data from OpenWeather API.
* **IR Mode:** Automatically disconnects WiFi to prevent interrupts, allowing the ESP8266 to focus 100% of its processing power on decoding/cloning IR signals.
* **Storage:** Uses SPIFFS (Internal Flash) to save your custom-named IR remotes so they are remembered after a restart.

## 📱 Tech Stack
- **Hardware:** ESP8266, ILI9341 TFT Touchscreen, IR Receiver/Transmitter.
- **Languages:** C++ (Arduino)
- **Key Concepts:** REST APIs, JSON Parsing, Non-volatile memory management.

## 🚀 Setup
1. Enter your WiFi and API credentials.
2. Flash to ESP8266 using Arduino IDE.
