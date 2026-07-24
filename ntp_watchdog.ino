/*
  WiFi Router Watchdog with NTP, ArduinoOTA, Hardware Task WDT & Web/HASS API
  Supports ESP32, ESP32-S2, ESP32-C3, ESP32-S3, and ESP8266 (NodeMCU v2)
*/

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <WiFiUdp.h>
  #include <ArduinoOTA.h>
  #include <esp_task_wdt.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  #define WDT_TIMEOUT_SEC 15
  WebServer server(80);
#elif defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiUdp.h>
  #include <ArduinoOTA.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPClient.h>
  ESP8266WebServer server(80);
#else
  #error "Unsupported platform! Please use ESP32 / ESP32-S2 / ESP32-C3 / ESP32-S3 or ESP8266."
#endif

#ifndef STASSID
#define STASSID "YOUR_WIFI_SSID"
#define STAPSK  "YOUR_WIFI_PASSWORD"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "YOUR_OTA_PASSWORD"
#endif

#if defined(BOARD_ESP32S3_SUPERMINI) || defined(CONFIG_IDF_TARGET_ESP32S3)
  #define LED_PIN   48   // Onboard LED on ESP32-S3 SuperMini (GPIO48)
  #define RELAY_PIN 4   // Relay control pin (GPIO4)
#elif defined(BOARD_ESP32C3_DEVKIT) || defined(CONFIG_IDF_TARGET_ESP32C3)
  #define LED_PIN   48   // Onboard LED on ESP32-S3 SuperMini (GPIO48)
  #define RELAY_PIN 4   // Relay control pin (GPIO4)
#elif defined(BOARD_ESP32S2_DEV) || defined(CONFIG_IDF_TARGET_ESP32S2)
  #define LED_PIN   15  // Onboard LED on ESP32-S2 (GPIO15)
  #define RELAY_PIN 4   // Relay control pin (GPIO4)
#elif defined(BOARD_ESP32_DEV) || defined(CONFIG_IDF_TARGET_ESP32)
  #define LED_PIN   2   // Onboard LED on ESP32 DevKit (GPIO2)
  #define RELAY_PIN 4   // Relay control pin (GPIO4)
#elif defined(BOARD_ESP8266_NODEMCU) || defined(ESP8266)
  #define LED_PIN   D4  // NodeMCU onboard LED (GPIO2 / D4)
  #define RELAY_PIN D2  // NodeMCU relay pin (GPIO4 / D2)
#else
  #define LED_PIN   2
  #define LED_PIN   48   // Onboard LED on ESP32-S3 SuperMini (GPIO48)
  #define RELAY_PIN 4
#endif

#define RELAY_TRIGGER_ON  HIGH
#define RELAY_TRIGGER_OFF LOW

#define DELAY_RESTART_SEC (24UL * 60UL * 60UL)
#define DELAY_POLL_SEC    (60UL)
#define WAIT_CONNECT_SEC  (60)
#define MAX_RETRY_COUNT   (10)

#ifndef SYSLOG_SERVER
#define SYSLOG_SERVER "192.168.1.50"
#define SYSLOG_PORT   514
#endif

#ifndef HASS_WEBHOOK_URL
#define HASS_WEBHOOK_URL "http://192.168.1.50:8123/api/webhook/wifi_router_watchdog_heartbeat"
#endif

const char* ssid      = STASSID;
const char* pass      = STAPSK;
const char* ntpServer = "pool.ntp.org";

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

WiFiUDP udp;
WiFiUDP syslogUdp;
unsigned int localPort = 2390;

enum WatchdogState {
  STATE_INIT,
  STATE_WAIT_WIFI,
  STATE_POLL_NTP,
  STATE_POWER_CYCLE,
  STATE_COOLDOWN
};

WatchdogState currentState = STATE_INIT;
unsigned long stateTimer = 0;
unsigned long lastPollTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long cooldownStartTime = 0;
unsigned long lastNtpEpoch = 0;
int retryCount = 0;

String get_state_name(WatchdogState st) {
  switch (st) {
    case STATE_INIT: return "INIT";
    case STATE_WAIT_WIFI: return "WAIT_WIFI";
    case STATE_POLL_NTP: return "POLL_NTP";
    case STATE_POWER_CYCLE: return "POWER_CYCLE";
    case STATE_COOLDOWN: return "COOLDOWN";
    default: return "UNKNOWN";
  }
}

String get_formatted_uptime() {
  unsigned long totalSeconds = millis() / 1000;
  unsigned long days = totalSeconds / 86400;
  unsigned long hours = (totalSeconds % 86400) / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%lud %02luh %02lum %02lus", days, hours, minutes, seconds);
  return String(buf);
}

void feed_hardware_wdt() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#elif defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  ESP.wdtFeed();
#endif
}

void send_syslog(const char* message) {
  if (WiFi.status() == WL_CONNECTED) {
    syslogUdp.beginPacket(SYSLOG_SERVER, SYSLOG_PORT);
    syslogUdp.print("<14>Watchdog-ESP: ");
    syslogUdp.print(message);
    syslogUdp.endPacket();
  }
}

void send_ha_heartbeat() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    if (http.begin(client, HASS_WEBHOOK_URL)) {
      http.addHeader("Content-Type", "application/json");
      String payload = "{\"status\":\"online\",\"uptime_seconds\":";
      payload += String(millis() / 1000);
      payload += ",\"uptime\":\"" + get_formatted_uptime() + "\",\"rssi\":";
      payload += String(WiFi.RSSI());
      payload += ",\"state\":\"" + get_state_name(currentState) + "\",\"retry_count\":";
      payload += String(retryCount);
      payload += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";

      int code = http.POST(payload);
      http.end();
      Serial.printf("[HASS] Heartbeat sent. Code: %d\n", code);
    }
  }
}

void sendNTPpacket(IPAddress& address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  udp.beginPacket(address, 123);
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void handle_status_json() {
  String json = "{\"status\":\"online\",\"uptime_seconds\":";
  json += String(millis() / 1000);
  json += ",\"uptime\":\"" + get_formatted_uptime() + "\",\"rssi\":";
  json += String(WiFi.RSSI());
  json += ",\"state\":\"" + get_state_name(currentState) + "\",\"retry_count\":";
  json += String(retryCount);
  json += ",\"max_retries\":";
  json += String(MAX_RETRY_COUNT);
  json += ",\"last_epoch\":";
  json += String(lastNtpEpoch);
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";

  server.send(200, "application/json", json);
}

void handle_root() {
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>Wi-Fi Router Watchdog</title>";
  html += "<style>body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:20px;}";
  html += ".card{background:#1e293b;border-radius:12px;padding:24px;max-width:500px;margin:20px auto;box-shadow:0 10px 15px -3px rgba(0,0,0,0.3);}";
  html += "h1{color:#38bdf8;font-size:1.5rem;margin-top:0;}";
  html += ".stat{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #334155;}";
  html += ".value{font-weight:bold;color:#34d399;}";
  html += ".btn{display:inline-block;background:#ef4444;color:#fff;padding:10px 20px;border-radius:8px;text-decoration:none;font-weight:bold;margin-top:15px;text-align:center;}";
  html += "</style></head><body>";
  html += "<div class=\"card\">";
  html += "<h1>🛠️ Wi-Fi Router Watchdog</h1>";
  html += "<div class=\"stat\"><span>Status</span><span class=\"value\">ONLINE</span></div>";
  html += "<div class=\"stat\"><span>Uptime</span><span class=\"value\'>" + get_formatted_uptime() + "</span></div>";
  html += "<div class=\"stat\"><span>Wi-Fi Signal</span><span class=\"value\'>" + String(WiFi.RSSI()) + " dBm</span></div>";
  html += "<div class=\"stat\"><span>State</span><span class=\"value\'>" + get_state_name(currentState) + "</span></div>";
  html += "<div class=\"stat\"><span>Retries</span><span class=\"value\'>" + String(retryCount) + " / " + String(MAX_RETRY_COUNT) + "</span></div>";
  html += "<div class=\"stat\"><span>IP Address</span><span class=\"value\'>" + WiFi.localIP().toString() + "</span></div>";
  html += "<a href=\"/reboot\" class=\"btn\">Reboot ESP Watchdog</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handle_reboot() {
  server.send(200, "text/plain", "Rebooting ESP Watchdog...");
  delay(1000);
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  ESP.restart();
#elif defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  ESP.reset();
#endif
}

void setup_web_server() {
  server.on("/", handle_root);
  server.on("/status", handle_status_json);
  server.on("/reboot", handle_reboot);
  server.begin();
  Serial.println("HTTP Server started on port 80");
}

void setup_ota() {
  ArduinoOTA.setHostname("wifi-router-watchdog");
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println(String("OTA Start updating ") + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  digitalWrite(RELAY_PIN, RELAY_TRIGGER_OFF);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("\n--- WiFi Router Watchdog Initializing ---");

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&twdt_config);
    esp_task_wdt_add(NULL);
  #else
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
  #endif
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  stateTimer = millis();
  currentState = STATE_WAIT_WIFI;
}

void loop() {
  feed_hardware_wdt();
  ArduinoOTA.handle();
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  unsigned long currentMillis = millis();

  switch (currentState) {
    case STATE_WAIT_WIFI: {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\nWiFi Connected! IP: ");
        Serial.println(WiFi.localIP());

        udp.stop();
        syslogUdp.stop();
        udp.begin(localPort);
        syslogUdp.begin(localPort + 1);

        setup_ota();
        setup_web_server();
        send_syslog("Watchdog booted and connected to WiFi");
        send_ha_heartbeat();

        currentState = STATE_POLL_NTP;
        lastPollTime = millis() - (DELAY_POLL_SEC * 1000);
        lastHeartbeatTime = millis();
      } else if (currentMillis - stateTimer >= (WAIT_CONNECT_SEC * 1000)) {
        Serial.println("\nWiFi Connection Timeout. Triggering Power Cycle.");
        send_syslog("WiFi connection timeout");
        currentState = STATE_POWER_CYCLE;
      }
      break;
    }

    case STATE_POLL_NTP: {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost. Reconnecting...");
        WiFi.begin(ssid, pass);
        stateTimer = millis();
        currentState = STATE_WAIT_WIFI;
        break;
      }

      // Periodic Heartbeat every 60s
      if (currentMillis - lastHeartbeatTime >= (DELAY_POLL_SEC * 1000)) {
        lastHeartbeatTime = currentMillis;
        send_ha_heartbeat();
      }

      if (currentMillis - lastPollTime >= (DELAY_POLL_SEC * 1000)) {
        lastPollTime = currentMillis;

        IPAddress timeServerIP;
        if (!WiFi.hostByName(ntpServer, timeServerIP)) {
          Serial.println("NTP DNS lookup failed!");
          send_syslog("DNS lookup failed");
          retryCount++;
          if (retryCount >= MAX_RETRY_COUNT) {
            retryCount = 0;
            currentState = STATE_POWER_CYCLE;
          }
          break;
        }

        sendNTPpacket(timeServerIP);
        delay(200);

        int cb = udp.parsePacket();
        if (!cb || cb < NTP_PACKET_SIZE) {
          retryCount++;
          Serial.printf("NTP Query failed. Retry %d/%d\n", retryCount, MAX_RETRY_COUNT);
          char syslogMsg[64];
          snprintf(syslogMsg, sizeof(syslogMsg), "NTP query failed (%d/%d)", retryCount, MAX_RETRY_COUNT);
          send_syslog(syslogMsg);

          if (retryCount >= MAX_RETRY_COUNT) {
            retryCount = 0;
            currentState = STATE_POWER_CYCLE;
          }
        } else {
          retryCount = 0;
          udp.read(packetBuffer, NTP_PACKET_SIZE);
          unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
          unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
          unsigned long secsSince1900 = highWord << 16 | lowWord;
          lastNtpEpoch = secsSince1900 - 2208988800UL;

          Serial.printf("NTP Response OK. Unix Epoch: %lu\n", lastNtpEpoch);
          digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
      }
      break;
    }

    case STATE_POWER_CYCLE: {
      Serial.println("[RELAY] Disconnecting router power (NC open)... ");
      send_syslog("Power cycling router now");

      digitalWrite(RELAY_PIN, RELAY_TRIGGER_ON);
      delay(5000);
      digitalWrite(RELAY_PIN, RELAY_TRIGGER_OFF);

      Serial.println("[RELAY] Router power restored. Entering 24h cooldown.");
      send_syslog("Router power restored. Entering 24-hour cooldown.");

      cooldownStartTime = millis();
      currentState = STATE_COOLDOWN;
      break;
    }

    case STATE_COOLDOWN: {
      if (currentMillis - cooldownStartTime >= (DELAY_RESTART_SEC * 1000)) {
        Serial.println("24-hour cooldown complete. Resuming watchdog polling.");
        send_syslog("Cooldown complete. Resuming watchdog polling.");
        currentState = STATE_POLL_NTP;
      }
      break;
    }

    default:
      currentState = STATE_INIT;
      break;
  }
}
