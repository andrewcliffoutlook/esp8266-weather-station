#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPUpdateServer.h>
#include "FS.h"
#include "OpenWeatherMapClient.h"


//******************************
// Start Settings
//******************************

// Weather Configuration
boolean DISPLAYWEATHER = true; // true = show weather when not printing / false = no weather
String WeatherApiKey = ""; // Your API Key from http://openweathermap.org/
// Default City Location (use http://openweathermap.org/find to find city ID)
int CityIDs[] = { 2643743 }; //Only USE ONE for weather marquee
// Languages: ar, bg, ca, cz, de, el, en, fa, fi, fr, gl, hr, hu, it, ja, kr, la, lt, mk, nl, pl, pt, ro, ru, se, sk, sl, es, tr, ua, vi, zh_cn, zh_tw
String WeatherLanguage = "en";  //Default (en) English

// Webserver
const int WEBSERVER_PORT = 80; // The port you can access this device on over HTTP
const boolean WEBSERVER_ENABLED = true;  // Device will provide a web interface via http://[ip]:[port]/
boolean IS_BASIC_AUTH = false;  // true = require athentication to change configuration settings / false = no auth
char* www_username = "admin";  // User account for the Web Interface
char* www_password = "password";  // Password for the Web Interface


// Date and Time
float UtcOffset = 0; // Hour offset from GMT for your timezone
boolean IS_24HOUR = false;     // 23:00 millitary 24 hour clock
int minutesBetweenDataRefresh = 15;
boolean DISPLAYCLOCK = true;   // true = Show Clock when not printing / false = turn off display when not printing
boolean INVERT_DISPLAY = false; // true = pins at top | false = pins at the bottom

// OTA Updates
boolean ENABLE_OTA = true;     // this will allow you to load firmware to the device over WiFi (see OTA for ESP8266)
String OTA_Password = "";      // Set an OTA password here -- leave blank if you don't want to be prompted for password

//******************************
// End Settings
//******************************

String themeColor = "light-green"; // this can be changed later in the web interface.
