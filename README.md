# 🛠️ Wi-Fi Router Watchdog (NTP, ArduinoOTA, Syslog & Task WDT)

An ESP32-S3 / ESP8266 hardware watchdog that monitors network connectivity via **UDP NTP timestamp queries** and logs watchdog events to a remote **UDP Syslog Server**. If persistent network failures or DNS resolution loss occur (`MAX_RETRY_COUNT = 10`), it power-cycles the router via a relay connected to the **Normally Closed (NC)** power loop, followed by an asynchronous 24-hour cooldown.

---

## 🌟 Key Features

1. **Multi-Architecture Support**:
   - **ESP32-S3 SuperMini** (`esp32-s3-devkitc-1`, 4MB Flash, CDC USB Serial).
   - **ESP8266** (`nodemcuv2`).
2. **Hardware Task Watchdog (WDT)**:
   - Configured with `esp_task_wdt` (ESP32-S3) / `ESP.wdtFeed()` (ESP8266) to prevent CPU lockups or firmware freezes.
3. **ArduinoOTA Support**:
   - Wireless over-the-air firmware updates (Hostname: `wifi-router-watchdog`, Port: `8266`, Password: `admin`).
4. **Non-Blocking State Machine**:
   - Uses asynchronous `millis()` timing to maintain Wi-Fi, ArduinoOTA, and WDT background tasks during the 24-hour cooldown period.
5. **UDP Syslog Remote Logging**:
   - Automatically sends boot, query status, connection timeout, and power cycle logs to remote syslog server.
6. **Configurable Relay Polarity**:
   - `RELAY_TRIGGER_ON` (HIGH) / `RELAY_TRIGGER_OFF` (LOW) for Normally Closed (NC) relay fail-safe architecture.

---

## 📌 Hardware Pinouts & Connections

### ESP32-S3 SuperMini
- **Relay Signal**: `GPIO 4`
- **Status LED**: `GPIO 8` (Onboard LED)
- **USB Interface**: `/dev/ttyACM0` (CDC Serial / JTAG)

### NodeMCU v2 (ESP8266)
- **Relay Signal**: `D2` (`GPIO 4`)
- **Status LED**: `D4` (`GPIO 2`)

---

## 🔌 Wiring Diagram

```
AC Power Source -> (NC Terminal) Relay Module (Common Terminal) -> Wi-Fi Router / Modem Power Supply
```
*Note: Using Normally Closed (NC) wiring ensures that if the ESP watchdog is powered off or resetting, power continues to flow uninterrupted to the router.*

---

## 🚀 Building & Flashing with PlatformIO

Compiling and uploading inside Distrobox container `resolute`:

### 1. Compile Firmware
```bash
# Compile ESP32-S3 SuperMini environment
pio run -e esp32-s3-supermini

# Compile NodeMCU ESP8266 environment
pio run -e nodemcuv2
```

### 2. Flash via USB Serial
```bash
# Upload to ESP32-S3 attached on /dev/ttyACM0
pio run -e esp32-s3-supermini -t upload
```

### 3. Wireless OTA Update
```bash
# Upload over Wi-Fi
pio run -e esp32-s3-supermini -t upload --upload-port DEVICE_IP
```

---

## 📊 Remote Syslog Verification

To monitor live watchdog logs on your syslog server:
```bash
cat /var/log/syslog/watchdog.log
```

Example log entries:
```log
=== Syslog Server Started ===
[2026-07-22 15:14:36] [192.168.1.X:2391] <14>Watchdog-ESP: Watchdog booted and connected to WiFi
```
