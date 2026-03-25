# IoT Smart-Station & IR Command Center 🌦️📡

A high-performance desktop companion built on the **ESP8266**. This project functions as a real-time weather station and an Intelligent Infrared (IR) Cloner with a custom-built Touch GUI.

![Project Hero Image](images/dashboard.jpg)

## 🚀 Unique Features
* **Context-Aware Resource Management:** The system intelligently deactivates the WiFi stack during IR capture. This eliminates CPU interrupts, ensuring microsecond precision for decoding IR protocols (NEC, Samsung, Sony).
* **On-Screen HMI Keyboard:** A custom-coded QWERTY touch interface allows users to name captured IR signals directly on the device.
* **Non-Volatile Storage (SPIFFS):** Captured signals and their custom names are saved to the ESP8266's internal Flash memory, ensuring data persists after power loss.
* **Dynamic Weather Engine:** Fetches live data (Temp, Humidity, Conditions) from OpenWeatherMap API with NTP time synchronization.

## 🛠️ Tech Stack
- **Microcontroller:** ESP8266 (NodeMCU)
- **Display:** 2.4" TFT LCD (ILI9341) using `TFT_eSPI`
- **Peripherals:** IR Receiver (TSOP), IR Transmitter LED
- **Communication:** REST API (JSON parsing via `ArduinoJson`), NTP
- **File System:** SPIFFS (Serial Peripheral Interface Flash File System)

## 📸 Interface Preview
| Dashboard Mode | Keyboard & IR Mode |
| :--- | :--- |
| ![Dashboard](images/dashboard.jpg) | ![IR Mode](images/ir_mode) | ![Keyboard](images/keyboard.jpg) |
| *Real-time Weather & NTP Clock* | *Captured IR signal* | Naming a signal |

## 🔧 Installation & Setup
1.  **Hardware:** Connect your TFT and IR modules as per the `circuit_diagram.png`.
2.  **Configuration:** Enter your WiFi SSID, Password, and OpenWeatherMap API Key.
3.  **Calibration:** On first boot, the system will trigger a touch-screen calibration. Follow the on-screen crosses to save your settings to SPIFFS.

## 📄 License
This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

---
**Developed by [Rithwik Nambiar](https://github.com/rithwik-nambiar)** *Aspiring Embedded Systems Engineer*
