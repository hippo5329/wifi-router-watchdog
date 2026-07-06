/*

  ntp_watchdog base on Udp NTP Client

  Get the time from a Network Time Protocol (NTP) time server
  Demonstrates use of UDP sendPacket and ReceivePacket
  For more on NTP time servers and the messages needed to communicate with them,
  see http://en.wikipedia.org/wiki/Network_Time_Protocol

  created 4 Sep 2010
  by Michael Margolis
  modified 9 Apr 2012
  by Tom Igoe
  updated for the ESP8266 12 Apr 2015
  by Ivan Grokhotkov

  This code is in the public domain.

*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#ifndef STASSID
#define STASSID "my_ssid"
#define STAPSK  "my_wifi_password"
#endif

#define LED D4
#define RELAY D2
#define DELAY_RESTART (24UL * 60UL * 60UL) // 24 hours in seconds
#define DELAY_START (5 * 60)
#define DELAY_POLL 60
#define WAIT_CONNECT 60
#define RETRY 10

#ifndef SYSLOG_SERVER
#define SYSLOG_SERVER "192.168.1.50"
#define SYSLOG_PORT 514
#endif

const char * ssid = STASSID; // your network SSID (name)
const char * pass = STAPSK;  // your network password

unsigned int localPort = 2390;      // local port to listen for UDP packets

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "pool.ntp.org";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;
WiFiUDP syslogUdp;

void blink(unsigned long i, int t1, int t2) {
  for (unsigned long n = 0; n < i; n++) {
    Serial.print(".");
    digitalWrite(LED, HIGH);  // Turn LED on
    delay(t1);
    Serial.print(".");
    digitalWrite(LED, LOW);  // Turn LED off  
    delay(t2);
    yield(); // Ensure background tasks/watchdog feed
  }
}

void send_syslog(const char* message) {
  if (WiFi.status() == WL_CONNECTED) {
    syslogUdp.beginPacket(SYSLOG_SERVER, SYSLOG_PORT);
    syslogUdp.print("<14>ESP8266-Watchdog: ");
    syslogUdp.print(message);
    syslogUdp.endPacket();
  }
}

void restart_router(void) {
  Serial.println("Turn off router");
  send_syslog("NTP/WiFi connection failed. Power cycling router.");
  delay(100); // Allow UDP packet to send
  digitalWrite(RELAY, HIGH);   // Turn relay on, disconnect router power
  blink(10, 250, 250);
  Serial.println("Turn on router");
  digitalWrite(RELAY, LOW);  // Turn relay off
  
  Serial.println("Entering 24-hour cooldown period...");
  blink(DELAY_RESTART, 100, 900);
  Serial.println("Cooldown finished. Restarting watchdog.");
}

void setup() {
  digitalWrite(RELAY, LOW);  // Turn relay off
  pinMode(RELAY, OUTPUT);     // Initialize the relay pin as an output
  digitalWrite(LED, HIGH);  // Turn LED on
  pinMode(LED, OUTPUT);     // Initialize the LED pin as an output
  
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  
  blink(DELAY_START, 950, 50);
}

void loop() {
  static int retryCount = 0;

  blink(DELAY_POLL, 900, 100);
  Serial.println();

  // Connect to WiFi if not already connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);

    int n = 0;
    while (WiFi.status() != WL_CONNECTED) {
      if (++n > WAIT_CONNECT) {
        Serial.println("\nWiFi connection timed out. Restarting router.");
        restart_router();
        return; // restart the loop
      }
      blink(1, 700, 300);
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    // Restart UDP sockets on reconnection
    udp.stop();
    syslogUdp.stop();
    Serial.println("Starting UDP");
    if (udp.begin(localPort)) {
      Serial.print("Local port: ");
      Serial.println(udp.localPort());
    } else {
      Serial.println("Failed to start UDP");
    }
    syslogUdp.begin(localPort + 1);
    send_syslog("WiFi connected and watchdog logging active");
  }

  // Lookup the IP address for the host name
  Serial.println("Resolving NTP address...");
  if (!WiFi.hostByName(ntpServerName, timeServerIP)) {
    Serial.println("DNS lookup failed.");
    send_syslog("DNS lookup failed");
    retryCount++;
    if (retryCount > RETRY) {
      retryCount = 0;
      restart_router();
    }
    return;
  }
  
  Serial.print("NTP address: ");
  Serial.println(timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  
  // wait to see if a reply is available
  blink(20, 150, 350);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.print("no packet yet, retry ");
    Serial.println(retryCount);
    
    char syslogMsg[64];
    snprintf(syslogMsg, sizeof(syslogMsg), "NTP query failed, retry %d/%d", retryCount + 1, RETRY);
    send_syslog(syslogMsg);

    retryCount++;
    if (retryCount > RETRY) {
      retryCount = 0;
      restart_router();
    }
  } else {
    retryCount = 0;
    Serial.print("packet received, length=");
    Serial.println(cb);
    send_syslog("NTP query successful");
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = ");
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if (((epoch % 3600) / 60) < 10) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ((epoch % 60) < 10) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
