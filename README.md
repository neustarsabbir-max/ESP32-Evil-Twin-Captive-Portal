# Esp32-Evil-Portal
Advanced ESP32 Evil Twin Attack & Captive Portal framework for WiFi Penetration Testing. Features include DNS Hijacking, SPIFFS credential logging, custom HTML phishing pages, and a mobile-responsive Admin UI. Educational security tool for ESP32
# ESP32 Evil Twin & Captive Portal Suite

![License](https://img.shields.io/badge/license-MIT-blue.svg) ![Platform](https://img.shields.io/badge/platform-ESP32-orange.svg) ![Author](https://img.shields.io/badge/Author-Sabbir%20SEU%20EEE-red)

A standalone WiFi Penetration Testing tool for ESP32. This project creates an "Evil Twin" Access Point that mimics legitimate networks to capture credentials via a realistic Captive Portal.

> [!WARNING]
> **DISCLAIMER:** This project is for **educational purposes and security research only**. Usage of this tool for attacking targets without prior mutual consent is illegal. The author assumes no liability and is not responsible for any misuse or damage caused by this program.

## ⚡ Features

* **Network Scanning:** Scans for available WiFi networks (AP + STA mode).
* **Evil Twin Attack:** Clones SSID and creates an open Access Point.
* **DNS Hijacking:** Redirects all traffic (Google, Apple, Android captive checks) to the phishing page.
* **Credential Harvesting:** Logs usernames/passwords to internal SPIFFS storage.
* **Mobile-Friendly Admin UI:** Control the device from your phone.
* **Global Channel Support:** Unlocked channels 1-13 (CN/JP region fix).
* **Custom HTML Support:** Upload your own phishing pages via the Admin Interface.
* **Persistent Configuration:** Settings saved in memory.

## 🛠️ Hardware Required

* **ESP32 Development Board** (ESP32-WROOM-32 or similar).
* USB Cable for power/programming.

## 💾 Installation

1.  **Install Arduino IDE:** Ensure you have the [ESP32 Board Manager](https://dl.espressif.com/dl/package_esp32_index.json) installed.
2.  **Libraries:** This sketch uses built-in ESP32 libraries:
    * `WiFi.h`
    * `WebServer.h`
    * `DNSServer.h`
    * `SPIFFS.h`
3.  **Partition Scheme:**
    * In Arduino IDE, go to **Tools > Partition Scheme**.
    * Select **"Default 4MB with SPIFFS"** (or any scheme that includes SPIFFS).
4.  **Upload:** Connect your ESP32 and upload the `Evil_Portal.ino`.

## 🚀 How to Use

### 1. Connect to Admin Panel
1. Power on the ESP32.
2. Connect your phone/PC to the WiFi network: `WiFi_Pentest`.
3. Password: `password123`.
4. Open a browser and navigate to: `http://192.168.4.1/admin`.

### 2. Launch an Attack
1. Go to the **Scan** tab to find targets.
2. Click **Select** on the target network.
3. Click **START** in the control panel.
4. The ESP32 will now broadcast the target SSID.

### 3. View Logs
1. When a victim connects and enters a password, it is saved.
2. Refresh the **Admin Panel** to view captured credentials in the "Captured Data" section.
3. Logs are persistent (saved to flash memory) until you click **Clear**.

## 📸 Screenshots

| Admin Dashboard | Attack Config |
|:---:|:---:|
Admin UI
| *[Add screen![6154475466027371373](https://github.com/user-attachments/assets/6d04a204-ce3d-401a-a988-4cc8f39ba523)

## 👨‍💻 Author

**Sabbir**
* Department of Electrical and Electronic Engineering (EEE)
* Southeast University (SEU), Batch 43

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
