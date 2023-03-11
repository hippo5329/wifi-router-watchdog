This is a wifi-router watchdog based on an esp8266 wifi relay board. It will connect to ntp server and power cycle the wifi router when internet connection failed. This requires more communication than a simple ping. After restart it will wait 20+ hours before next trigger to avoid power cycle too frequencely.

I used a board similar to the "HiLetgo ESP8266 WiFi Relay Module DC7-30V ESP-12F Network 5V Relay Module 220V 10A", which can be found on Amazon. I soldered pin headers of uart to program the board. Any esp8266/esp32 board can be used with the relay control pin redefined. The are some esp8266 relay boards come with usb programming interface, which will be easier to use.

The relay outputs should be connect to AC supply using the "normal close" and "common" terminals. That is, the router power is always connected until the esp8266 interrupts it when the watchdog timeout. The failure of esp8266 will not harm the normal router operation.

An extra "24-Hour Mechanical Outlet Timer" can be added the reset the esp8266 as a fail-safe. Cycle power to esp8266 several times a day to restart the esp8266. (I cycle the power to esp8266 4 times a day.)  Cycle power to esp8266 won't affect the power to the router, as we use "normal close" connection. It happened that both the esp8266 and router failed after AC power glitch on my site.

AC power =>  Mechanical Outlet Timer => DC power adaptor => esp8266 power

AC power => (optional UPS =>) esp8266 relay (NC) => cable modem and wifi router etc.

The LED blinks at different duty cycles as a simple monitor of the states of the esp8266 watchdog.
