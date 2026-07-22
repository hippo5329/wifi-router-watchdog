# 🛠️ Wi-Fi Router Watchdog (NTP, ArduinoOTA, Syslog, Web API & Task WDT)

An ESP32-S3 / ESP8266 hardware watchdog that monitors network connectivity via **UDP NTP timestamp queries** and logs watchdog events to a remote **UDP Syslog Server**. If persistent network failures or DNS resolution loss occur (`MAX_RETRY_COUNT = 10`), it power-cycles the router via a relay connected to the **Normally Closed (NC)** power loop, followed by an asynchronous 24-hour cooldown.

---

## 🌟 Key Features

1. **Multi-Architecture Support**:
   - **ESP32-S3 SuperMini** (`esp32-s3-devkitc-1`, 4MB Flash, CDC USB Serial).
   - **ESP8266** (`nodemcuv2`).
2. **Web Server & REST API**:
   - Web Dashboard on Port 80 (`http://<DEVICE_IP>/`).
   - JSON REST API endpoint at `http://<DEVICE_IP>/status` returning real-time status, uptime, Wi-Fi RSSI, state machine state, and fail counts.
3. **Home Assistant Integration**:
   - Built-in support for Home Assistant REST & Template Sensors (`hass-5317` or any HA instance).
   - Automatic HTTP POST heartbeat client.
4. **Hardware Task Watchdog (WDT)**:
   - Configured with `esp_task_wdt` (ESP32-S3) / `ESP.wdtFeed()` (ESP8266) to prevent CPU lockups or firmware freezes.
5. **ArduinoOTA Support**:
   - Wireless over-the-air firmware updates (Hostname: `wifi-router-watchdog`, Password: `admin`).
6. **Non-Blocking State Machine**:
   - Uses asynchronous `millis()` timing to maintain Wi-Fi, Web Server, ArduinoOTA, and WDT background tasks during the 24-hour cooldown period.
7. **UDP Syslog Remote Logging**:
   - Automatically sends boot, query status, connection timeout, and power cycle logs to remote syslog server.

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
# Upload over Wi-Fi with authentication
export PLATFORMIO_UPLOAD_FLAGS="-aadmin"
pio run -e esp32-s3-supermini -t upload --upload-port DEVICE_IP
```

---

## 🌐 Web Server & REST API Endpoints

- `GET http://<DEVICE_IP>/` -> Interactive HTML Web Status Dashboard
- `GET http://<DEVICE_IP>/status` -> JSON API:
  ```json
  {
    "status": "online",
    "uptime_seconds": 3600,
    "uptime": "0d 01h 00m 00s",
    "rssi": -52,
    "state": "POLL_NTP",
    "retry_count": 0,
    "max_retries": 10,
    "last_epoch": 1784733276,
    "ip": "192.168.1.50"
  }
  ```
- `GET http://<DEVICE_IP>/reboot` -> Triggers ESP restart.

---

## 🏠 Home Assistant Configuration (`configuration.yaml`)

Add the following to Home Assistant to monitor the watchdog UI:

```yaml
sensor:
  - platform: rest
    name: "NTP Watchdog Status"
    resource: "http://<DEVICE_IP>/status"
    scan_interval: 10
    timeout: 5
    value_template: "{{ value_json.status }}"
    json_attributes:
      - status
      - uptime_seconds
      - uptime
      - rssi
      - state
      - retry_count
      - max_retries
      - last_epoch
      - ip

template:
  - sensor:
      - name: "NTP Watchdog Uptime"
        state: "{{ state_attr('sensor.ntp_watchdog_status', 'uptime') }}"
        icon: mdi:clock-outline

      - name: "NTP Watchdog Wi-Fi Signal"
        state: "{{ state_attr('sensor.ntp_watchdog_status', 'rssi') }}"
        unit_of_measurement: "dBm"
        device_class: signal_strength
        icon: mdi:wifi

      - name: "NTP Watchdog State"
        state: "{{ state_attr('sensor.ntp_watchdog_status', 'state') }}"
        icon: mdi:chip
```
