/*
  WiFi Router Watchdog with NTP, ArduinoOTA, and Hardware Task Watchdog
  Supports ESP32-S3 (ESP32-S3 SuperMini) and ESP8266 (NodeMCU v2)
*/

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <WiFiUdp.h>
  #include <ArduinoOTA.h>
  #include <esp_task_wdt.h>
  #define WDT_TIMEOUT_SEC 15
#elif defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiUdp.h>
  #include <ArduinoOTA.h>
#else
  #error "Unsupported platform! Please use ESP32 or ESP8266."
#endif

#ifndef STASSID
#define STASSID "YOUR_WIFI_SSID"
#define STAPSK  "YOUR_WIFI_PASSWORD"
#endif

#if defined(BOARD_ESP32S3_SUPERMINI) || defined(CONFIG_IDF_TARGET_ESP32S3)
  #define LED_PIN   8   // Onboard LED on ESP32-S3 SuperMini (GPIO8)
  #define RELAY_PIN 4   // Relay control pin (GPIO4)
#elif defined(BOARD_ESP8266_NODEMCU) || defined(ESP8266)
  #define LED_PIN   D4  // NodeMCU onboard LED (GPIO2 / D4)
  #define RELAY_PIN D2  // NodeMCU relay pin (GPIO4 / D2)
#else
  #define LED_PIN   8
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
unsigned long cooldownStartTime = 0;
int retryCount = 0;

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

void setup_ota() {
  ArduinoOTA.setHostname("wifi-router-watchdog");
  ArduinoOTA.setPassword("admin");

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
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
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
        send_syslog("Watchdog booted and connected to WiFi");

        currentState = STATE_POLL_NTP;
        lastPollTime = millis() - (DELAY_POLL_SEC * 1000);
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
          unsigned long epoch = secsSince1900 - 2208988800UL;

          Serial.printf("NTP Response OK. Unix Epoch: %lu\n", epoch);
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
