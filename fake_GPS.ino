// Required for wifi_station_connect() to work
extern "C" {
#include "user_interface.h"
}

#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#include <WiFiUdp.h>
#include <time.h>

// needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>    // https://github.com/arduino-libraries/NTPClient
#include <Timezone.h>     // https://github.com/JChristensen/Timezone
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager

// WeMos D1 Mini LED is inverted, so I'm using a define here for LED states so I
// don't go insane Flip these for a normal board.
#define LED_OFF HIGH
#define LED_ON LOW
#define GPS_BAUD 9600

/*
 * Config option 1: Local time output
 *
 * Leave your clock's internal timezone offset unconfigured, and this option
 * will set it correctly.
 *
 * Configure your timezone rules below as per library doco here:
 * https://github.com/JChristensen/Timezone
 */
TimeChangeRule myDT = {"AEDT", First, Sun, Oct, 2, +660};  // Australian Eastern Daylight Time, UTC+11
TimeChangeRule myST = {"AEST", First, Sun, Apr, 2, +600};  // Australian Eastern Standard Time, UTC+10

/*
 * Config option 2: DST-compensated UTC output
 *
 * This requires setting a base timezone offset in your clock's configuration,
 * but the NTP client will still auto-adjust for DST. May be useful if you're
 * feeding this into a transmitter for multiple clocks set to different
 * timezones, or your clock's TZ is set already.
 *
 * Update the DST start/end rules, but not offsets, as per library doco linked
 * above.
 */
// TimeChangeRule myDT = { "LDT", First, Sun, Oct, 2, +60 };  // During DST,
// broadcast UTC+1 TimeChangeRule myST = { "LST", First, Sun, Apr, 2, +0 };   //
// Outside DST, broadcast UTC

/*
 * Config option 3: Raw UTC, all the time
 *
 * This behaves the same as the original PVelectronics/nixiekits.eu NTP
 * transmitter, sending raw UTC time. Requires setting a timezone offset in your
 * clock, and manually enabling/disabling DST.
 *
 * No change required other than to uncomment the two lines below. Yes, setting
 * two +0 timezones is a dirty hack. Fight me.
 */
// TimeChangeRule myDT = { "UTC", First, Sun, Oct, 2, +0 };  // Broadcast UTC
// TimeChangeRule myST = { "UTD", First, Sun, Apr, 2, +0 };  // Broadcast UTC

// Declare timezone based on config set above
Timezone myTZ(myDT, myST);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "au.pool.ntp.org", 0, 60000);
WiFiManager wifiManager;

// first-run monitoring var
bool firstrun = true;

void setup() {
    // Initialize LED pin, turn on LED, and start USB-Serial console
    //pinMode(LED_BUILTIN, OUTPUT); // D1 mini LED_BUILTIN is the same as Serial1 TX, so we can't use it here - but fortunately that makes the LED blink when we send GPS data anyway.
    //digitalWrite(LED_BUILTIN, LED_ON);
    Serial.begin(115200);

    // Set timeout for wifi connection/configuration
    wifiManager.setTimeout(300);

    /*
     * Run wifiManager auto-reconnection routine.
     * Attempts to connect to last known WiFi network. On fail, creates a softAP
     * for reconfiguration. Default IP for softAP is 192.168.4.1, and has no
     * password.
     */
    if (!wifiManager.autoConnect("NixieAP")) {
        Serial.println("Timed out connecting to WiFi, resetting...");
        delay(3000);
        // reset and try again
        ESP.reset();
        delay(5000);
    }

    // WiFi has connected successfully, log to console and turn off LED
    Serial.println("WiFi connected, woo!");
    //digitalWrite(LED_BUILTIN, LED_OFF);

    // Start FakeGPS output serial port
    Serial1.begin(GPS_BAUD);

    // Trigger NTP client update
    timeClient.update();
}

void loop() {
    char tstr[128];
    unsigned char cs;
    unsigned int i;
    time_t rawtime, loctime;
    unsigned long amicros, umicros = 0;

    for (;;) {
        amicros = micros();
        if (timeClient.update())  // NTP-update
        {
            umicros = amicros;
            rawtime = timeClient.getEpochTime();  // get NTP-time
            loctime = myTZ.toLocal(rawtime);      // calc local time

            if ((!second(loctime)) || firstrun)  // if first-run, or a full minute has passed, output
            {
                //digitalWrite(LED_BUILTIN, LED_ON);  // disabled for D1 Mini
                sprintf(tstr, "$GPRMC,%02d%02d%02d,A,3748.31792,S,14510.91109,E,0.0,0.0,%02d%02d%02d,0.0,E,S",
                        hour(loctime), minute(loctime), second(loctime), day(loctime), month(loctime), year(loctime) - 2000);
                cs = 0;
                for (i = 1; i < strlen(tstr); i++)  // calculate checksum
                    cs ^= tstr[i];
                sprintf(tstr + strlen(tstr), "*%02X", cs);
                Serial.println(tstr);   // send to console
                Serial1.println(tstr);  // send to clock
                delay(100);
                //digitalWrite(LED_BUILTIN, LED_OFF);  // sync process done, turn off LED
                delay(58000 - ((micros() - amicros) / 1000) - (second(loctime) * 1000));  // wait for end of minute
                if (firstrun) {
                    firstrun = false;
                }  // clear firstrun flag
            }
        }
        delay(200);
        if (((amicros - umicros) / 1000000L) > 3600) { // if no sync for more than one hour
            digitalWrite(LED_BUILTIN, LED_ON);  // set LED on to indicate things are sad
        }
    }
}
