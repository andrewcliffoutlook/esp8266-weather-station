/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

#include <Arduino.h>
#define ESP8266
//#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <coredecls.h>                  // settimeofday_cb()
//#else
//#include <WiFi.h>
//#endif
#include <ESPHTTPClient.h>
#include <JsonListener.h>

// time
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include "Settings.h"
#include "SSD1306Wire.h"
#include "SH1106Wire.h"
#include "OLEDDisplayUi.h"
#include "Wire.h"
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"


/***************************
 * Begin Settings
 **************************/

// WIFI
const char* WIFI_SSID = "XXXX";
const char* WIFI_PWD = "XXXX";

#define HOSTNAME "Weather-" 
#define CONFIG "/conf.txt"

#define TZ              0       // (utc+) TZ in hours
#define DST_MN          0      // use 60mn for summer time in some countries

// Setup
const int UPDATE_INTERVAL_SECS = 20 * 60; // Update every 20 minutes

// Time management variables - SC Addition
unsigned long previousMillis = 0; // stores the last time RSSI was updated
const long interval = 60000;       // interval to update RSSI (e.g., every 5000 ms = 5 seconds)

// Display Settings
const int I2C_DISPLAY_ADDRESS = 0x3C;
#if defined(ESP8266)
const int SDA_PIN = D2;
const int SDC_PIN = D1;
//boolean INVERT_DISPLAY = false; // true = pins at top | false = pins at the bottom
//#else
//const int SDA_PIN = 5; //D3;
//const int SDC_PIN = 4; //D4;
#endif


// OpenWeatherMap Settings
// Sign up here to get an API key:
// https://docs.thingpulse.com/how-tos/openweathermap-key/
String OPEN_WEATHER_MAP_APP_ID = "XXXX";
/*
Go to https://openweathermap.org/find?q= and search for a location. Go through the
result set and select the entry closest to the actual location you want to display 
data for. It'll be a URL like https://openweathermap.org/city/2657896. The number
at the end is what you assign to the constant below.
 */
String OPEN_WEATHER_MAP_LOCATION_ID = "6693242";

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.
String OPEN_WEATHER_MAP_LANGUAGE = "en";
const uint8_t MAX_FORECASTS = 4;

boolean IS_METRIC = true;

// Adjust according to your language
const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

/***************************
 * End Settings
 **************************/
 // Initialize the oled display for address 0x3c
 // sda-pin=14 and sdc-pin=12
 SH1106Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
 OLEDDisplayUi   ui( &display );

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

// Weather Client
OpenWeatherMapClient weatherClient(WeatherApiKey, CityIDs, 1, IS_METRIC, WeatherLanguage);

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)
time_t now;

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

long timeSinceLastWUpdate = 0;
bool updateRssi = false;
int8_t rssi = WiFi.RSSI();

//declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();


// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
FrameCallback frames[] = { drawDateTime, drawCurrentWeather, drawForecast };
int numberOfFrames = 3;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

//declaring prototypes
void configModeCallback (WiFiManager *myWiFiManager);
int8_t getWifiQuality();

ESP8266WebServer server(WEBSERVER_PORT);
ESP8266HTTPUpdateServer serverUpdater;

static const char WEB_ACTIONS[] PROGMEM =  "<a class='w3-bar-item w3-button' href='/'><i class='fa fa-home'></i> Home</a>"
                      "<a class='w3-bar-item w3-button' href='/configure'><i class='fa fa-cog'></i> Configure</a>"
                      "<a class='w3-bar-item w3-button' href='/configureweather'><i class='fa fa-cloud'></i> Weather</a>"
                      "<a class='w3-bar-item w3-button' href='/systemreset' onclick='return confirm(\"Do you want to reset to default settings?\")'><i class='fa fa-undo'></i> Reset Settings</a>"
                      "<a class='w3-bar-item w3-button' href='/forgetwifi' onclick='return confirm(\"Do you want to forget to WiFi connection?\")'><i class='fa fa-wifi'></i> Forget WiFi</a>"
                      "<a class='w3-bar-item w3-button' href='/update'><i class='fa fa-wrench'></i> Firmware Update</a>";

String CHANGE_FORM =  ""; // moved to config to make it dynamic

static const char CLOCK_FORM[] PROGMEM = "<p>Weather Refresh (minutes) <select class='w3-option w3-padding' name='refresh'>%OPTIONS%</select></p>";
                            
static const char THEME_FORM[] PROGMEM =   "<p>Theme Color <select class='w3-option w3-padding' name='theme'>%THEME_OPTIONS%</select></p>"
                      "<p>Time Zone <select class='w3-option w3-padding' name='timezone'>%TIME_ZONE_OPTIONS%</select></p>"
                      "<p><input name='isBasicAuth' class='w3-check w3-margin-top' type='checkbox' %IS_BASICAUTH_CHECKED%> Use Security Credentials for Configuration Changes</p>"
                      "<p><label>User ID (for this interface)</label><input class='w3-input w3-border w3-margin-bottom' type='text' name='userid' value='%USERID%' maxlength='20'></p>"
                      "<p><label>Password </label><input class='w3-input w3-border w3-margin-bottom' type='password' name='stationpassword' value='%STATIONPASSWORD%'></p>"
                      "<button class='w3-button w3-block w3-grey w3-section w3-padding' type='submit'>Save</button></form>";

static const char WEATHER_FORM[] PROGMEM = "<form class='w3-container' action='/updateweatherconfig' method='get'><h2>Weather Config:</h2>"
                      "<p><input name='isWeatherEnabled' class='w3-check w3-margin-top' type='checkbox' %IS_WEATHER_CHECKED%> Display Weather </p>"
                      "<label>OpenWeatherMap API Key (get from <a href='https://openweathermap.org/' target='_BLANK'>here</a>)</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='openWeatherMapApiKey' value='%WEATHERKEY%' maxlength='60'>"
                      "<p><label>%CITYNAME1% (<a href='http://openweathermap.org/find' target='_BLANK'><i class='fa fa-search'></i> Search for City ID</a>) "
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='city1' value='%CITY1%' onkeypress='return isNumberKey(event)'></p>"
                      "<p><input name='metric' class='w3-check w3-margin-top' type='checkbox' %METRIC%> Use Metric (Celsius)</p>"
                      "<p>Weather Language <select class='w3-option w3-padding' name='language'>%LANGUAGEOPTIONS%</select></p>"
                      "<button class='w3-button w3-block w3-grey w3-section w3-padding' type='submit'>Save</button></form>"
                      "<script>function isNumberKey(e){var h=e.which?e.which:event.keyCode;return!(h>31&&(h<48||h>57))}</script>";

static const char LANG_OPTIONS[] PROGMEM = "<option>ar</option>"
                      "<option>bg</option>"
                      "<option>ca</option>"
                      "<option>cz</option>"
                      "<option>de</option>"
                      "<option>el</option>"
                      "<option>en</option>"
                      "<option>fa</option>"
                      "<option>fi</option>"
                      "<option>fr</option>"
                      "<option>gl</option>"
                      "<option>hr</option>"
                      "<option>hu</option>"
                      "<option>it</option>"
                      "<option>ja</option>"
                      "<option>kr</option>"
                      "<option>la</option>"
                      "<option>lt</option>"
                      "<option>mk</option>"
                      "<option>nl</option>"
                      "<option>pl</option>"
                      "<option>pt</option>"
                      "<option>ro</option>"
                      "<option>ru</option>"
                      "<option>se</option>"
                      "<option>sk</option>"
                      "<option>sl</option>"
                      "<option>es</option>"
                      "<option>tr</option>"
                      "<option>ua</option>"
                      "<option>vi</option>"
                      "<option>zh_cn</option>"
                      "<option>zh_tw</option>";

static const char COLOR_THEMES[] PROGMEM = "<option>red</option>"
                      "<option>pink</option>"
                      "<option>purple</option>"
                      "<option>deep-purple</option>"
                      "<option>indigo</option>"
                      "<option>blue</option>"
                      "<option>light-blue</option>"
                      "<option>cyan</option>"
                      "<option>teal</option>"
                      "<option>green</option>"
                      "<option>light-green</option>"
                      "<option>lime</option>"
                      "<option>khaki</option>"
                      "<option>yellow</option>"
                      "<option>amber</option>"
                      "<option>orange</option>"
                      "<option>deep-orange</option>"
                      "<option>blue-grey</option>"
                      "<option>brown</option>"
                      "<option>grey</option>"
                      "<option>dark-grey</option>"
                      "<option>black</option>"
                      "<option>w3schools</option>";
                      
static const char TIME_ZONES[] PROGMEM = "<option value='Africa/Abidjan|GMT0'>Africa/Abidjan</option>"
                      "<option value='Africa/Accra|GMT0'>Africa/Accra</option>"
                      "<option value='Africa/Addis_Ababa|EAT-3'>Africa/Addis_Ababa</option>"
                      "<option value='Africa/Algiers|CET-1'>Africa/Algiers</option>"
                      "<option value='Africa/Asmara|EAT-3'>Africa/Asmara</option>"
                      "<option value='Africa/Bamako|GMT0'>Africa/Bamako</option>"
                      "<option value='Africa/Bangui|WAT-1'>Africa/Bangui</option>"
                      "<option value='Africa/Banjul|GMT0'>Africa/Banjul</option>"
                      "<option value='Africa/Bissau|GMT0'>Africa/Bissau</option>"
                      "<option value='Africa/Blantyre|CAT-2'>Africa/Blantyre</option>"
                      "<option value='Africa/Brazzaville|WAT-1'>Africa/Brazzaville</option>"
                      "<option value='Africa/Bujumbura|CAT-2'>Africa/Bujumbura</option>"
                      "<option value='Africa/Cairo|EET-2EEST,M4.5.5/0,M10.5.4/24'>Africa/Cairo</option>"
                      "<option value='Africa/Casablanca|<+01>-1'>Africa/Casablanca</option>"
                      "<option value='Africa/Ceuta|CET-1CEST,M3.5.0,M10.5.0/3'>Africa/Ceuta</option>"
                      "<option value='Africa/Conakry|GMT0'>Africa/Conakry</option>"
                      "<option value='Africa/Dakar|GMT0'>Africa/Dakar</option>"
                      "<option value='Africa/Dar_es_Salaam|EAT-3'>Africa/Dar_es_Salaam</option>"
                      "<option value='Africa/Djibouti|EAT-3'>Africa/Djibouti</option>"
                      "<option value='Africa/Douala|WAT-1'>Africa/Douala</option>"
                      "<option value='Africa/El_Aaiun|<+01>-1'>Africa/El_Aaiun</option>"
                      "<option value='Africa/Freetown|GMT0'>Africa/Freetown</option>"
                      "<option value='Africa/Gaborone|CAT-2'>Africa/Gaborone</option>"
                      "<option value='Africa/Harare|CAT-2'>Africa/Harare</option>"
                      "<option value='Africa/Johannesburg|SAST-2'>Africa/Johannesburg</option>"
                      "<option value='Africa/Juba|CAT-2'>Africa/Juba</option>"
                      "<option value='Africa/Kampala|EAT-3'>Africa/Kampala</option>"
                      "<option value='Africa/Khartoum|CAT-2'>Africa/Khartoum</option>"
                      "<option value='Africa/Kigali|CAT-2'>Africa/Kigali</option>"
                      "<option value='Africa/Kinshasa|WAT-1'>Africa/Kinshasa</option>"
                      "<option value='Africa/Lagos|WAT-1'>Africa/Lagos</option>"
                      "<option value='Africa/Libreville|WAT-1'>Africa/Libreville</option>"
                      "<option value='Africa/Lome|GMT0'>Africa/Lome</option>"
                      "<option value='Africa/Luanda|WAT-1'>Africa/Luanda</option>"
                      "<option value='Africa/Lubumbashi|CAT-2'>Africa/Lubumbashi</option>"
                      "<option value='Africa/Lusaka|CAT-2'>Africa/Lusaka</option>"
                      "<option value='Africa/Malabo|WAT-1'>Africa/Malabo</option>"
                      "<option value='Africa/Maputo|CAT-2'>Africa/Maputo</option>"
                      "<option value='Africa/Maseru|SAST-2'>Africa/Maseru</option>"
                      "<option value='Africa/Mbabane|SAST-2'>Africa/Mbabane</option>"
                      "<option value='Africa/Mogadishu|EAT-3'>Africa/Mogadishu</option>"
                      "<option value='Africa/Monrovia|GMT0'>Africa/Monrovia</option>"
                      "<option value='Africa/Nairobi|EAT-3'>Africa/Nairobi</option>"
                      "<option value='Africa/Ndjamena|WAT-1'>Africa/Ndjamena</option>"
                      "<option value='Africa/Niamey|WAT-1'>Africa/Niamey</option>"
                      "<option value='Africa/Nouakchott|GMT0'>Africa/Nouakchott</option>"
                      "<option value='Africa/Ouagadougou|GMT0'>Africa/Ouagadougou</option>"
                      "<option value='Africa/Porto-Novo|WAT-1'>Africa/Porto-Novo</option>"
                      "<option value='Africa/Sao_Tome|GMT0'>Africa/Sao_Tome</option>"
                      "<option value='Africa/Tripoli|EET-2'>Africa/Tripoli</option>"
                      "<option value='Africa/Tunis|CET-1'>Africa/Tunis</option>"
                      "<option value='Africa/Windhoek|CAT-2'>Africa/Windhoek</option>"
                      "<option value='America/Adak|HST10HDT,M3.2.0,M11.1.0'>America/Adak</option>"
                      "<option value='America/Anchorage|AKST9AKDT,M3.2.0,M11.1.0'>America/Anchorage</option>"
                      "<option value='America/Anguilla|AST4'>America/Anguilla</option>"
                      "<option value='America/Antigua|AST4'>America/Antigua</option>"
                      "<option value='America/Araguaina|<-03>3'>America/Araguaina</option>"
                      "<option value='America/Argentina/Buenos_Aires|<-03>3'>America/Argentina/Buenos_Aires</option>"
                      "<option value='America/Argentina/Catamarca|<-03>3'>America/Argentina/Catamarca</option>"
                      "<option value='America/Argentina/Cordoba|<-03>3'>America/Argentina/Cordoba</option>"
                      "<option value='America/Argentina/Jujuy|<-03>3'>America/Argentina/Jujuy</option>"
                      "<option value='America/Argentina/La_Rioja|<-03>3'>America/Argentina/La_Rioja</option>"
                      "<option value='America/Argentina/Mendoza|<-03>3'>America/Argentina/Mendoza</option>"
                      "<option value='America/Argentina/Rio_Gallegos|<-03>3'>America/Argentina/Rio_Gallegos</option>"
                      "<option value='America/Argentina/Salta|<-03>3'>America/Argentina/Salta</option>"
                      "<option value='America/Argentina/San_Juan|<-03>3'>America/Argentina/San_Juan</option>"
                      "<option value='America/Argentina/San_Luis|<-03>3'>America/Argentina/San_Luis</option>"
                      "<option value='America/Argentina/Tucuman|<-03>3'>America/Argentina/Tucuman</option>"
                      "<option value='America/Argentina/Ushuaia|<-03>3'>America/Argentina/Ushuaia</option>"
                      "<option value='America/Aruba|AST4'>America/Aruba</option>"
                      "<option value='America/Asuncion|<-04>4<-03>,M10.1.0/0,M3.4.0/0'>America/Asuncion</option>"
                      "<option value='America/Atikokan|EST5'>America/Atikokan</option>"
                      "<option value='America/Bahia|<-03>3'>America/Bahia</option>"
                      "<option value='America/Bahia_Banderas|CST6'>America/Bahia_Banderas</option>"
                      "<option value='America/Barbados|AST4'>America/Barbados</option>"
                      "<option value='America/Belem|<-03>3'>America/Belem</option>"
                      "<option value='America/Belize|CST6'>America/Belize</option>"
                      "<option value='America/Blanc-Sablon|AST4'>America/Blanc-Sablon</option>"
                      "<option value='America/Boa_Vista|<-04>4'>America/Boa_Vista</option>"
                      "<option value='America/Bogota|<-05>5'>America/Bogota</option>"
                      "<option value='America/Boise|MST7MDT,M3.2.0,M11.1.0'>America/Boise</option>"
                      "<option value='America/Cambridge_Bay|MST7MDT,M3.2.0,M11.1.0'>America/Cambridge_Bay</option>"
                      "<option value='America/Campo_Grande|<-04>4'>America/Campo_Grande</option>"
                      "<option value='America/Cancun|EST5'>America/Cancun</option>"
                      "<option value='America/Caracas|<-04>4'>America/Caracas</option>"
                      "<option value='America/Cayenne|<-03>3'>America/Cayenne</option>"
                      "<option value='America/Cayman|EST5'>America/Cayman</option>"
                      "<option value='America/Chicago|CST6CDT,M3.2.0,M11.1.0'>America/Chicago</option>"
                      "<option value='America/Chihuahua|CST6'>America/Chihuahua</option>"
                      "<option value='America/Costa_Rica|CST6'>America/Costa_Rica</option>"
                      "<option value='America/Creston|MST7'>America/Creston</option>"
                      "<option value='America/Cuiaba|<-04>4'>America/Cuiaba</option>"
                      "<option value='America/Curacao|AST4'>America/Curacao</option>"
                      "<option value='America/Danmarkshavn|GMT0'>America/Danmarkshavn</option>"
                      "<option value='America/Dawson|MST7'>America/Dawson</option>"
                      "<option value='America/Dawson_Creek|MST7'>America/Dawson_Creek</option>"
                      "<option value='America/Denver|MST7MDT,M3.2.0,M11.1.0'>America/Denver</option>"
                      "<option value='America/Detroit|EST5EDT,M3.2.0,M11.1.0'>America/Detroit</option>"
                      "<option value='America/Dominica|AST4'>America/Dominica</option>"
                      "<option value='America/Edmonton|MST7MDT,M3.2.0,M11.1.0'>America/Edmonton</option>"
                      "<option value='America/Eirunepe|<-05>5'>America/Eirunepe</option>"
                      "<option value='America/El_Salvador|CST6'>America/El_Salvador</option>"
                      "<option value='America/Fortaleza|<-03>3'>America/Fortaleza</option>"
                      "<option value='America/Fort_Nelson|MST7'>America/Fort_Nelson</option>"
                      "<option value='America/Glace_Bay|AST4ADT,M3.2.0,M11.1.0'>America/Glace_Bay</option>"
                      "<option value='America/Godthab|<-02>2<-01>,M3.5.0/-1,M10.5.0/0'>America/Godthab</option>"
                      "<option value='America/Goose_Bay|AST4ADT,M3.2.0,M11.1.0'>America/Goose_Bay</option>"
                      "<option value='America/Grand_Turk|EST5EDT,M3.2.0,M11.1.0'>America/Grand_Turk</option>"
                      "<option value='America/Grenada|AST4'>America/Grenada</option>"
                      "<option value='America/Guadeloupe|AST4'>America/Guadeloupe</option>"
                      "<option value='America/Guatemala|CST6'>America/Guatemala</option>"
                      "<option value='America/Guayaquil|<-05>5'>America/Guayaquil</option>"
                      "<option value='America/Guyana|<-04>4'>America/Guyana</option>"
                      "<option value='America/Halifax|AST4ADT,M3.2.0,M11.1.0'>America/Halifax</option>"
                      "<option value='America/Havana|CST5CDT,M3.2.0/0,M11.1.0/1'>America/Havana</option>"
                      "<option value='America/Hermosillo|MST7'>America/Hermosillo</option>"
                      "<option value='America/Indiana/Indianapolis|EST5EDT,M3.2.0,M11.1.0'>America/Indiana/Indianapolis</option>"
                      "<option value='America/Indiana/Knox|CST6CDT,M3.2.0,M11.1.0'>America/Indiana/Knox</option>"
                      "<option value='America/Indiana/Marengo|EST5EDT,M3.2.0,M11.1.0'>America/Indiana/Marengo</option>"
                      "<option value='America/Indiana/Petersburg|EST5EDT,M3.2.0,M11.1.0'>America/Indiana/Petersburg</option>"
                      "<option value='America/Indiana/Tell_City|CST6CDT,M3.2.0,M11.1.0'>America/Indiana/Tell_City</option>"
                      "<option value='America/Indiana/Vevay|EST5EDT,M3.2.0,M11.1.0'>America/Indiana/Vevay</option>"
                      "<option value='America/Indiana/Vincennes|EST5EDT,M3.2.0,M11.1.0'>America/Indiana/Vincennes</option>"
                      "<option value='America/Indiana/Winamac|EST5EDT,M3.2.0,M11.1.0'>America/Indiana/Winamac</option>"
                      "<option value='America/Inuvik|MST7MDT,M3.2.0,M11.1.0'>America/Inuvik</option>"
                      "<option value='America/Iqaluit|EST5EDT,M3.2.0,M11.1.0'>America/Iqaluit</option>"
                      "<option value='America/Jamaica|EST5'>America/Jamaica</option>"
                      "<option value='America/Juneau|AKST9AKDT,M3.2.0,M11.1.0'>America/Juneau</option>"
                      "<option value='America/Kentucky/Louisville|EST5EDT,M3.2.0,M11.1.0'>America/Kentucky/Louisville</option>"
                      "<option value='America/Kentucky/Monticello|EST5EDT,M3.2.0,M11.1.0'>America/Kentucky/Monticello</option>"
                      "<option value='America/Kralendijk|AST4'>America/Kralendijk</option>"
                      "<option value='America/La_Paz|<-04>4'>America/La_Paz</option>"
                      "<option value='America/Lima|<-05>5'>America/Lima</option>"
                      "<option value='America/Los_Angeles|PST8PDT,M3.2.0,M11.1.0'>America/Los_Angeles</option>"
                      "<option value='America/Lower_Princes|AST4'>America/Lower_Princes</option>"
                      "<option value='America/Maceio|<-03>3'>America/Maceio</option>"
                      "<option value='America/Managua|CST6'>America/Managua</option>"
                      "<option value='America/Manaus|<-04>4'>America/Manaus</option>"
                      "<option value='America/Marigot|AST4'>America/Marigot</option>"
                      "<option value='America/Martinique|AST4'>America/Martinique</option>"
                      "<option value='America/Matamoros|CST6CDT,M3.2.0,M11.1.0'>America/Matamoros</option>"
                      "<option value='America/Mazatlan|MST7'>America/Mazatlan</option>"
                      "<option value='America/Menominee|CST6CDT,M3.2.0,M11.1.0'>America/Menominee</option>"
                      "<option value='America/Merida|CST6'>America/Merida</option>"
                      "<option value='America/Metlakatla|AKST9AKDT,M3.2.0,M11.1.0'>America/Metlakatla</option>"
                      "<option value='America/Mexico_City|CST6'>America/Mexico_City</option>"
                      "<option value='America/Miquelon|<-03>3<-02>,M3.2.0,M11.1.0'>America/Miquelon</option>"
                      "<option value='America/Moncton|AST4ADT,M3.2.0,M11.1.0'>America/Moncton</option>"
                      "<option value='America/Monterrey|CST6'>America/Monterrey</option>"
                      "<option value='America/Montevideo|<-03>3'>America/Montevideo</option>"
                      "<option value='America/Montreal|EST5EDT,M3.2.0,M11.1.0'>America/Montreal</option>"
                      "<option value='America/Montserrat|AST4'>America/Montserrat</option>"
                      "<option value='America/Nassau|EST5EDT,M3.2.0,M11.1.0'>America/Nassau</option>"
                      "<option value='America/New_York|EST5EDT,M3.2.0,M11.1.0'>America/New_York</option>"
                      "<option value='America/Nipigon|EST5EDT,M3.2.0,M11.1.0'>America/Nipigon</option>"
                      "<option value='America/Nome|AKST9AKDT,M3.2.0,M11.1.0'>America/Nome</option>"
                      "<option value='America/Noronha|<-02>2'>America/Noronha</option>"
                      "<option value='America/North_Dakota/Beulah|CST6CDT,M3.2.0,M11.1.0'>America/North_Dakota/Beulah</option>"
                      "<option value='America/North_Dakota/Center|CST6CDT,M3.2.0,M11.1.0'>America/North_Dakota/Center</option>"
                      "<option value='America/North_Dakota/New_Salem|CST6CDT,M3.2.0,M11.1.0'>America/North_Dakota/New_Salem</option>"
                      "<option value='America/Nuuk|<-02>2<-01>,M3.5.0/-1,M10.5.0/0'>America/Nuuk</option>"
                      "<option value='America/Ojinaga|CST6CDT,M3.2.0,M11.1.0'>America/Ojinaga</option>"
                      "<option value='America/Panama|EST5'>America/Panama</option>"
                      "<option value='America/Pangnirtung|EST5EDT,M3.2.0,M11.1.0'>America/Pangnirtung</option>"
                      "<option value='America/Paramaribo|<-03>3'>America/Paramaribo</option>"
                      "<option value='America/Phoenix|MST7'>America/Phoenix</option>"
                      "<option value='America/Port-au-Prince|EST5EDT,M3.2.0,M11.1.0'>America/Port-au-Prince</option>"
                      "<option value='America/Port_of_Spain|AST4'>America/Port_of_Spain</option>"
                      "<option value='America/Porto_Velho|<-04>4'>America/Porto_Velho</option>"
                      "<option value='America/Puerto_Rico|AST4'>America/Puerto_Rico</option>"
                      "<option value='America/Punta_Arenas|<-03>3'>America/Punta_Arenas</option>"
                      "<option value='America/Rainy_River|CST6CDT,M3.2.0,M11.1.0'>America/Rainy_River</option>"
                      "<option value='America/Rankin_Inlet|CST6CDT,M3.2.0,M11.1.0'>America/Rankin_Inlet</option>"
                      "<option value='America/Recife|<-03>3'>America/Recife</option>"
                      "<option value='America/Regina|CST6'>America/Regina</option>"
                      "<option value='America/Resolute|CST6CDT,M3.2.0,M11.1.0'>America/Resolute</option>"
                      "<option value='America/Rio_Branco|<-05>5'>America/Rio_Branco</option>"
                      "<option value='America/Santarem|<-03>3'>America/Santarem</option>"
                      "<option value='America/Santiago|<-04>4<-03>,M9.1.6/24,M4.1.6/24'>America/Santiago</option>"
                      "<option value='America/Santo_Domingo|AST4'>America/Santo_Domingo</option>"
                      "<option value='America/Sao_Paulo|<-03>3'>America/Sao_Paulo</option>"
                      "<option value='America/Scoresbysund|<-02>2<-01>,M3.5.0/-1,M10.5.0/0'>America/Scoresbysund</option>"
                      "<option value='America/Sitka|AKST9AKDT,M3.2.0,M11.1.0'>America/Sitka</option>"
                      "<option value='America/St_Barthelemy|AST4'>America/St_Barthelemy</option>"
                      "<option value='America/St_Johns|NST3:30NDT,M3.2.0,M11.1.0'>America/St_Johns</option>"
                      "<option value='America/St_Kitts|AST4'>America/St_Kitts</option>"
                      "<option value='America/St_Lucia|AST4'>America/St_Lucia</option>"
                      "<option value='America/St_Thomas|AST4'>America/St_Thomas</option>"
                      "<option value='America/St_Vincent|AST4'>America/St_Vincent</option>"
                      "<option value='America/Swift_Current|CST6'>America/Swift_Current</option>"
                      "<option value='America/Tegucigalpa|CST6'>America/Tegucigalpa</option>"
                      "<option value='America/Thule|AST4ADT,M3.2.0,M11.1.0'>America/Thule</option>"
                      "<option value='America/Thunder_Bay|EST5EDT,M3.2.0,M11.1.0'>America/Thunder_Bay</option>"
                      "<option value='America/Tijuana|PST8PDT,M3.2.0,M11.1.0'>America/Tijuana</option>"
                      "<option value='America/Toronto|EST5EDT,M3.2.0,M11.1.0'>America/Toronto</option>"
                      "<option value='America/Tortola|AST4'>America/Tortola</option>"
                      "<option value='America/Vancouver|PST8PDT,M3.2.0,M11.1.0'>America/Vancouver</option>"
                      "<option value='America/Whitehorse|MST7'>America/Whitehorse</option>"
                      "<option value='America/Winnipeg|CST6CDT,M3.2.0,M11.1.0'>America/Winnipeg</option>"
                      "<option value='America/Yakutat|AKST9AKDT,M3.2.0,M11.1.0'>America/Yakutat</option>"
                      "<option value='America/Yellowknife|MST7MDT,M3.2.0,M11.1.0'>America/Yellowknife</option>"
                      "<option value='Antarctica/Casey|<+08>-8'>Antarctica/Casey</option>"
                      "<option value='Antarctica/Davis|<+07>-7'>Antarctica/Davis</option>"
                      "<option value='Antarctica/DumontDUrville|<+10>-10'>Antarctica/DumontDUrville</option>"
                      "<option value='Antarctica/Macquarie|AEST-10AEDT,M10.1.0,M4.1.0/3'>Antarctica/Macquarie</option>"
                      "<option value='Antarctica/Mawson|<+05>-5'>Antarctica/Mawson</option>"
                      "<option value='Antarctica/McMurdo|NZST-12NZDT,M9.5.0,M4.1.0/3'>Antarctica/McMurdo</option>"
                      "<option value='Antarctica/Palmer|<-03>3'>Antarctica/Palmer</option>"
                      "<option value='Antarctica/Rothera|<-03>3'>Antarctica/Rothera</option>"
                      "<option value='Antarctica/Syowa|<+03>-3'>Antarctica/Syowa</option>"
                      "<option value='Antarctica/Troll|<+00>0<+02>-2,M3.5.0/1,M10.5.0/3'>Antarctica/Troll</option>"
                      "<option value='Antarctica/Vostok|<+05>-5'>Antarctica/Vostok</option>"
                      "<option value='Arctic/Longyearbyen|CET-1CEST,M3.5.0,M10.5.0/3'>Arctic/Longyearbyen</option>"
                      "<option value='Asia/Aden|<+03>-3'>Asia/Aden</option>"
                      "<option value='Asia/Almaty|<+05>-5'>Asia/Almaty</option>"
                      "<option value='Asia/Amman|<+03>-3'>Asia/Amman</option>"
                      "<option value='Asia/Anadyr|<+12>-12'>Asia/Anadyr</option>"
                      "<option value='Asia/Aqtau|<+05>-5'>Asia/Aqtau</option>"
                      "<option value='Asia/Aqtobe|<+05>-5'>Asia/Aqtobe</option>"
                      "<option value='Asia/Ashgabat|<+05>-5'>Asia/Ashgabat</option>"
                      "<option value='Asia/Atyrau|<+05>-5'>Asia/Atyrau</option>"
                      "<option value='Asia/Baghdad|<+03>-3'>Asia/Baghdad</option>"
                      "<option value='Asia/Bahrain|<+03>-3'>Asia/Bahrain</option>"
                      "<option value='Asia/Baku|<+04>-4'>Asia/Baku</option>"
                      "<option value='Asia/Bangkok|<+07>-7'>Asia/Bangkok</option>"
                      "<option value='Asia/Barnaul|<+07>-7'>Asia/Barnaul</option>"
                      "<option value='Asia/Beirut|EET-2EEST,M3.5.0/0,M10.5.0/0'>Asia/Beirut</option>"
                      "<option value='Asia/Bishkek|<+06>-6'>Asia/Bishkek</option>"
                      "<option value='Asia/Brunei|<+08>-8'>Asia/Brunei</option>"
                      "<option value='Asia/Chita|<+09>-9'>Asia/Chita</option>"
                      "<option value='Asia/Choibalsan|<+08>-8'>Asia/Choibalsan</option>"
                      "<option value='Asia/Colombo|<+0530>-5:30'>Asia/Colombo</option>"
                      "<option value='Asia/Damascus|<+03>-3'>Asia/Damascus</option>"
                      "<option value='Asia/Dhaka|<+06>-6'>Asia/Dhaka</option>"
                      "<option value='Asia/Dili|<+09>-9'>Asia/Dili</option>"
                      "<option value='Asia/Dubai|<+04>-4'>Asia/Dubai</option>"
                      "<option value='Asia/Dushanbe|<+05>-5'>Asia/Dushanbe</option>"
                      "<option value='Asia/Famagusta|EET-2EEST,M3.5.0/3,M10.5.0/4'>Asia/Famagusta</option>"
                      "<option value='Asia/Gaza|EET-2EEST,M3.4.4/50,M10.4.4/50'>Asia/Gaza</option>"
                      "<option value='Asia/Hebron|EET-2EEST,M3.4.4/50,M10.4.4/50'>Asia/Hebron</option>"
                      "<option value='Asia/Ho_Chi_Minh|<+07>-7'>Asia/Ho_Chi_Minh</option>"
                      "<option value='Asia/Hong_Kong|HKT-8'>Asia/Hong_Kong</option>"
                      "<option value='Asia/Hovd|<+07>-7'>Asia/Hovd</option>"
                      "<option value='Asia/Irkutsk|<+08>-8'>Asia/Irkutsk</option>"
                      "<option value='Asia/Jakarta|WIB-7'>Asia/Jakarta</option>"
                      "<option value='Asia/Jayapura|WIT-9'>Asia/Jayapura</option>"
                      "<option value='Asia/Jerusalem|IST-2IDT,M3.4.4/26,M10.5.0'>Asia/Jerusalem</option>"
                      "<option value='Asia/Kabul|<+0430>-4:30'>Asia/Kabul</option>"
                      "<option value='Asia/Kamchatka|<+12>-12'>Asia/Kamchatka</option>"
                      "<option value='Asia/Karachi|PKT-5'>Asia/Karachi</option>"
                      "<option value='Asia/Kathmandu|<+0545>-5:45'>Asia/Kathmandu</option>"
                      "<option value='Asia/Khandyga|<+09>-9'>Asia/Khandyga</option>"
                      "<option value='Asia/Kolkata|IST-5:30'>Asia/Kolkata</option>"
                      "<option value='Asia/Krasnoyarsk|<+07>-7'>Asia/Krasnoyarsk</option>"
                      "<option value='Asia/Kuala_Lumpur|<+08>-8'>Asia/Kuala_Lumpur</option>"
                      "<option value='Asia/Kuching|<+08>-8'>Asia/Kuching</option>"
                      "<option value='Asia/Kuwait|<+03>-3'>Asia/Kuwait</option>"
                      "<option value='Asia/Macau|CST-8'>Asia/Macau</option>"
                      "<option value='Asia/Magadan|<+11>-11'>Asia/Magadan</option>"
                      "<option value='Asia/Makassar|WITA-8'>Asia/Makassar</option>"
                      "<option value='Asia/Manila|PST-8'>Asia/Manila</option>"
                      "<option value='Asia/Muscat|<+04>-4'>Asia/Muscat</option>"
                      "<option value='Asia/Nicosia|EET-2EEST,M3.5.0/3,M10.5.0/4'>Asia/Nicosia</option>"
                      "<option value='Asia/Novokuznetsk|<+07>-7'>Asia/Novokuznetsk</option>"
                      "<option value='Asia/Novosibirsk|<+07>-7'>Asia/Novosibirsk</option>"
                      "<option value='Asia/Omsk|<+06>-6'>Asia/Omsk</option>"
                      "<option value='Asia/Oral|<+05>-5'>Asia/Oral</option>"
                      "<option value='Asia/Phnom_Penh|<+07>-7'>Asia/Phnom_Penh</option>"
                      "<option value='Asia/Pontianak|WIB-7'>Asia/Pontianak</option>"
                      "<option value='Asia/Pyongyang|KST-9'>Asia/Pyongyang</option>"
                      "<option value='Asia/Qatar|<+03>-3'>Asia/Qatar</option>"
                      "<option value='Asia/Qyzylorda|<+05>-5'>Asia/Qyzylorda</option>"
                      "<option value='Asia/Riyadh|<+03>-3'>Asia/Riyadh</option>"
                      "<option value='Asia/Sakhalin|<+11>-11'>Asia/Sakhalin</option>"
                      "<option value='Asia/Samarkand|<+05>-5'>Asia/Samarkand</option>"
                      "<option value='Asia/Seoul|KST-9'>Asia/Seoul</option>"
                      "<option value='Asia/Shanghai|CST-8'>Asia/Shanghai</option>"
                      "<option value='Asia/Singapore|<+08>-8'>Asia/Singapore</option>"
                      "<option value='Asia/Srednekolymsk|<+11>-11'>Asia/Srednekolymsk</option>"
                      "<option value='Asia/Taipei|CST-8'>Asia/Taipei</option>"
                      "<option value='Asia/Tashkent|<+05>-5'>Asia/Tashkent</option>"
                      "<option value='Asia/Tbilisi|<+04>-4'>Asia/Tbilisi</option>"
                      "<option value='Asia/Tehran|<+0330>-3:30'>Asia/Tehran</option>"
                      "<option value='Asia/Thimphu|<+06>-6'>Asia/Thimphu</option>"
                      "<option value='Asia/Tokyo|JST-9'>Asia/Tokyo</option>"
                      "<option value='Asia/Tomsk|<+07>-7'>Asia/Tomsk</option>"
                      "<option value='Asia/Ulaanbaatar|<+08>-8'>Asia/Ulaanbaatar</option>"
                      "<option value='Asia/Urumqi|<+06>-6'>Asia/Urumqi</option>"
                      "<option value='Asia/Ust-Nera|<+10>-10'>Asia/Ust-Nera</option>"
                      "<option value='Asia/Vientiane|<+07>-7'>Asia/Vientiane</option>"
                      "<option value='Asia/Vladivostok|<+10>-10'>Asia/Vladivostok</option>"
                      "<option value='Asia/Yakutsk|<+09>-9'>Asia/Yakutsk</option>"
                      "<option value='Asia/Yangon|<+0630>-6:30'>Asia/Yangon</option>"
                      "<option value='Asia/Yekaterinburg|<+05>-5'>Asia/Yekaterinburg</option>"
                      "<option value='Asia/Yerevan|<+04>-4'>Asia/Yerevan</option>"
                      "<option value='Atlantic/Azores|<-01>1<+00>,M3.5.0/0,M10.5.0/1'>Atlantic/Azores</option>"
                      "<option value='Atlantic/Bermuda|AST4ADT,M3.2.0,M11.1.0'>Atlantic/Bermuda</option>"
                      "<option value='Atlantic/Canary|WET0WEST,M3.5.0/1,M10.5.0'>Atlantic/Canary</option>"
                      "<option value='Atlantic/Cape_Verde|<-01>1'>Atlantic/Cape_Verde</option>"
                      "<option value='Atlantic/Faroe|WET0WEST,M3.5.0/1,M10.5.0'>Atlantic/Faroe</option>"
                      "<option value='Atlantic/Madeira|WET0WEST,M3.5.0/1,M10.5.0'>Atlantic/Madeira</option>"
                      "<option value='Atlantic/Reykjavik|GMT0'>Atlantic/Reykjavik</option>"
                      "<option value='Atlantic/South_Georgia|<-02>2'>Atlantic/South_Georgia</option>"
                      "<option value='Atlantic/Stanley|<-03>3'>Atlantic/Stanley</option>"
                      "<option value='Atlantic/St_Helena|GMT0'>Atlantic/St_Helena</option>"
                      "<option value='Australia/Adelaide|ACST-9:30ACDT,M10.1.0,M4.1.0/3'>Australia/Adelaide</option>"
                      "<option value='Australia/Brisbane|AEST-10'>Australia/Brisbane</option>"
                      "<option value='Australia/Broken_Hill|ACST-9:30ACDT,M10.1.0,M4.1.0/3'>Australia/Broken_Hill</option>"
                      "<option value='Australia/Currie|AEST-10AEDT,M10.1.0,M4.1.0/3'>Australia/Currie</option>"
                      "<option value='Australia/Darwin|ACST-9:30'>Australia/Darwin</option>"
                      "<option value='Australia/Eucla|<+0845>-8:45'>Australia/Eucla</option>"
                      "<option value='Australia/Hobart|AEST-10AEDT,M10.1.0,M4.1.0/3'>Australia/Hobart</option>"
                      "<option value='Australia/Lindeman|AEST-10'>Australia/Lindeman</option>"
                      "<option value='Australia/Lord_Howe|<+1030>-10:30<+11>-11,M10.1.0,M4.1.0'>Australia/Lord_Howe</option>"
                      "<option value='Australia/Melbourne|AEST-10AEDT,M10.1.0,M4.1.0/3'>Australia/Melbourne</option>"
                      "<option value='Australia/Perth|AWST-8'>Australia/Perth</option>"
                      "<option value='Australia/Sydney|AEST-10AEDT,M10.1.0,M4.1.0/3'>Australia/Sydney</option>"
                      "<option value='Europe/Amsterdam|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Amsterdam</option>"
                      "<option value='Europe/Andorra|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Andorra</option>"
                      "<option value='Europe/Astrakhan|<+04>-4'>Europe/Astrakhan</option>"
                      "<option value='Europe/Athens|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Athens</option>"
                      "<option value='Europe/Belgrade|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Belgrade</option>"
                      "<option value='Europe/Berlin|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Berlin</option>"
                      "<option value='Europe/Bratislava|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Bratislava</option>"
                      "<option value='Europe/Brussels|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Brussels</option>"
                      "<option value='Europe/Bucharest|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Bucharest</option>"
                      "<option value='Europe/Budapest|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Budapest</option>"
                      "<option value='Europe/Busingen|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Busingen</option>"
                      "<option value='Europe/Chisinau|EET-2EEST,M3.5.0,M10.5.0/3'>Europe/Chisinau</option>"
                      "<option value='Europe/Copenhagen|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Copenhagen</option>"
                      "<option value='Europe/Dublin|IST-1GMT0,M10.5.0,M3.5.0/1'>Europe/Dublin</option>"
                      "<option value='Europe/Gibraltar|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Gibraltar</option>"
                      "<option value='Europe/Guernsey|GMT0BST,M3.5.0/1,M10.5.0'>Europe/Guernsey</option>"
                      "<option value='Europe/Helsinki|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Helsinki</option>"
                      "<option value='Europe/Isle_of_Man|GMT0BST,M3.5.0/1,M10.5.0'>Europe/Isle_of_Man</option>"
                      "<option value='Europe/Istanbul|<+03>-3'>Europe/Istanbul</option>"
                      "<option value='Europe/Jersey|GMT0BST,M3.5.0/1,M10.5.0'>Europe/Jersey</option>"
                      "<option value='Europe/Kaliningrad|EET-2'>Europe/Kaliningrad</option>"
                      "<option value='Europe/Kiev|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Kiev</option>"
                      "<option value='Europe/Kirov|MSK-3'>Europe/Kirov</option>"
                      "<option value='Europe/Lisbon|WET0WEST,M3.5.0/1,M10.5.0'>Europe/Lisbon</option>"
                      "<option value='Europe/Ljubljana|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Ljubljana</option>"
                      "<option value='Europe/London|GMT0BST,M3.5.0/1,M10.5.0'>Europe/London</option>"
                      "<option value='Europe/Luxembourg|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Luxembourg</option>"
                      "<option value='Europe/Madrid|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Madrid</option>"
                      "<option value='Europe/Malta|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Malta</option>"
                      "<option value='Europe/Mariehamn|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Mariehamn</option>"
                      "<option value='Europe/Minsk|<+03>-3'>Europe/Minsk</option>"
                      "<option value='Europe/Monaco|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Monaco</option>"
                      "<option value='Europe/Moscow|MSK-3'>Europe/Moscow</option>"
                      "<option value='Europe/Oslo|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Oslo</option>"
                      "<option value='Europe/Paris|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Paris</option>"
                      "<option value='Europe/Podgorica|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Podgorica</option>"
                      "<option value='Europe/Prague|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Prague</option>"
                      "<option value='Europe/Riga|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Riga</option>"
                      "<option value='Europe/Rome|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Rome</option>"
                      "<option value='Europe/Samara|<+04>-4'>Europe/Samara</option>"
                      "<option value='Europe/San_Marino|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/San_Marino</option>"
                      "<option value='Europe/Sarajevo|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Sarajevo</option>"
                      "<option value='Europe/Saratov|<+04>-4'>Europe/Saratov</option>"
                      "<option value='Europe/Simferopol|MSK-3'>Europe/Simferopol</option>"
                      "<option value='Europe/Skopje|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Skopje</option>"
                      "<option value='Europe/Sofia|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Sofia</option>"
                      "<option value='Europe/Stockholm|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Stockholm</option>"
                      "<option value='Europe/Tallinn|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Tallinn</option>"
                      "<option value='Europe/Tirane|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Tirane</option>"
                      "<option value='Europe/Ulyanovsk|<+04>-4'>Europe/Ulyanovsk</option>"
                      "<option value='Europe/Uzhgorod|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Uzhgorod</option>"
                      "<option value='Europe/Vaduz|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Vaduz</option>"
                      "<option value='Europe/Vatican|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Vatican</option>"
                      "<option value='Europe/Vienna|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Vienna</option>"
                      "<option value='Europe/Vilnius|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Vilnius</option>"
                      "<option value='Europe/Volgograd|MSK-3'>Europe/Volgograd</option>"
                      "<option value='Europe/Warsaw|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Warsaw</option>"
                      "<option value='Europe/Zagreb|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Zagreb</option>"
                      "<option value='Europe/Zaporozhye|EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Zaporozhye</option>"
                      "<option value='Europe/Zurich|CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Zurich</option>"
                      "<option value='Indian/Antananarivo|EAT-3'>Indian/Antananarivo</option>"
                      "<option value='Indian/Chagos|<+06>-6'>Indian/Chagos</option>"
                      "<option value='Indian/Christmas|<+07>-7'>Indian/Christmas</option>"
                      "<option value='Indian/Cocos|<+0630>-6:30'>Indian/Cocos</option>"
                      "<option value='Indian/Comoro|EAT-3'>Indian/Comoro</option>"
                      "<option value='Indian/Kerguelen|<+05>-5'>Indian/Kerguelen</option>"
                      "<option value='Indian/Mahe|<+04>-4'>Indian/Mahe</option>"
                      "<option value='Indian/Maldives|<+05>-5'>Indian/Maldives</option>"
                      "<option value='Indian/Mauritius|<+04>-4'>Indian/Mauritius</option>"
                      "<option value='Indian/Mayotte|EAT-3'>Indian/Mayotte</option>"
                      "<option value='Indian/Reunion|<+04>-4'>Indian/Reunion</option>"
                      "<option value='Pacific/Apia|<+13>-13'>Pacific/Apia</option>"
                      "<option value='Pacific/Auckland|NZST-12NZDT,M9.5.0,M4.1.0/3'>Pacific/Auckland</option>"
                      "<option value='Pacific/Bougainville|<+11>-11'>Pacific/Bougainville</option>"
                      "<option value='Pacific/Chatham|<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45'>Pacific/Chatham</option>"
                      "<option value='Pacific/Chuuk|<+10>-10'>Pacific/Chuuk</option>"
                      "<option value='Pacific/Easter|<-06>6<-05>,M9.1.6/22,M4.1.6/22'>Pacific/Easter</option>"
                      "<option value='Pacific/Efate|<+11>-11'>Pacific/Efate</option>"
                      "<option value='Pacific/Enderbury|<+13>-13'>Pacific/Enderbury</option>"
                      "<option value='Pacific/Fakaofo|<+13>-13'>Pacific/Fakaofo</option>"
                      "<option value='Pacific/Fiji|<+12>-12'>Pacific/Fiji</option>"
                      "<option value='Pacific/Funafuti|<+12>-12'>Pacific/Funafuti</option>"
                      "<option value='Pacific/Galapagos|<-06>6'>Pacific/Galapagos</option>"
                      "<option value='Pacific/Gambier|<-09>9'>Pacific/Gambier</option>"
                      "<option value='Pacific/Guadalcanal|<+11>-11'>Pacific/Guadalcanal</option>"
                      "<option value='Pacific/Guam|ChST-10'>Pacific/Guam</option>"
                      "<option value='Pacific/Honolulu|HST10'>Pacific/Honolulu</option>"
                      "<option value='Pacific/Kiritimati|<+14>-14'>Pacific/Kiritimati</option>"
                      "<option value='Pacific/Kosrae|<+11>-11'>Pacific/Kosrae</option>"
                      "<option value='Pacific/Kwajalein|<+12>-12'>Pacific/Kwajalein</option>"
                      "<option value='Pacific/Majuro|<+12>-12'>Pacific/Majuro</option>"
                      "<option value='Pacific/Marquesas|<-0930>9:30'>Pacific/Marquesas</option>"
                      "<option value='Pacific/Midway|SST11'>Pacific/Midway</option>"
                      "<option value='Pacific/Nauru|<+12>-12'>Pacific/Nauru</option>"
                      "<option value='Pacific/Niue|<-11>11'>Pacific/Niue</option>"
                      "<option value='Pacific/Norfolk|<+11>-11<+12>,M10.1.0,M4.1.0/3'>Pacific/Norfolk</option>"
                      "<option value='Pacific/Noumea|<+11>-11'>Pacific/Noumea</option>"
                      "<option value='Pacific/Pago_Pago|SST11'>Pacific/Pago_Pago</option>"
                      "<option value='Pacific/Palau|<+09>-9'>Pacific/Palau</option>"
                      "<option value='Pacific/Pitcairn|<-08>8'>Pacific/Pitcairn</option>"
                      "<option value='Pacific/Pohnpei|<+11>-11'>Pacific/Pohnpei</option>"
                      "<option value='Pacific/Port_Moresby|<+10>-10'>Pacific/Port_Moresby</option>"
                      "<option value='Pacific/Rarotonga|<-10>10'>Pacific/Rarotonga</option>"
                      "<option value='Pacific/Saipan|ChST-10'>Pacific/Saipan</option>"
                      "<option value='Pacific/Tahiti|<-10>10'>Pacific/Tahiti</option>"
                      "<option value='Pacific/Tarawa|<+12>-12'>Pacific/Tarawa</option>"
                      "<option value='Pacific/Tongatapu|<+13>-13'>Pacific/Tongatapu</option>"
                      "<option value='Pacific/Wake|<+12>-12'>Pacific/Wake</option>"
                      "<option value='Pacific/Wallis|<+12>-12'>Pacific/Wallis</option>"
                      "<option value='Etc/GMT|GMT0'>Etc/GMT</option>"
                      "<option value='Etc/GMT-0|GMT0'>Etc/GMT-0</option>"
                      "<option value='Etc/GMT-1|<+01>-1'>Etc/GMT-1</option>"
                      "<option value='Etc/GMT-2|<+02>-2'>Etc/GMT-2</option>"
                      "<option value='Etc/GMT-3|<+03>-3'>Etc/GMT-3</option>"
                      "<option value='Etc/GMT-4|<+04>-4'>Etc/GMT-4</option>"
                      "<option value='Etc/GMT-5|<+05>-5'>Etc/GMT-5</option>"
                      "<option value='Etc/GMT-6|<+06>-6'>Etc/GMT-6</option>"
                      "<option value='Etc/GMT-7|<+07>-7'>Etc/GMT-7</option>"
                      "<option value='Etc/GMT-8|<+08>-8'>Etc/GMT-8</option>"
                      "<option value='Etc/GMT-9|<+09>-9'>Etc/GMT-9</option>"
                      "<option value='Etc/GMT-10|<+10>-10'>Etc/GMT-10</option>"
                      "<option value='Etc/GMT-11|<+11>-11'>Etc/GMT-11</option>"
                      "<option value='Etc/GMT-12|<+12>-12'>Etc/GMT-12</option>"
                      "<option value='Etc/GMT-13|<+13>-13'>Etc/GMT-13</option>"
                      "<option value='Etc/GMT-14|<+14>-14'>Etc/GMT-14</option>"
                      "<option value='Etc/GMT0|GMT0'>Etc/GMT0</option>"
                      "<option value='Etc/GMT+0|GMT0'>Etc/GMT+0</option>"
                      "<option value='Etc/GMT+1|<-01>1'>Etc/GMT+1</option>"
                      "<option value='Etc/GMT+2|<-02>2'>Etc/GMT+2</option>"
                      "<option value='Etc/GMT+3|<-03>3'>Etc/GMT+3</option>"
                      "<option value='Etc/GMT+4|<-04>4'>Etc/GMT+4</option>"
                      "<option value='Etc/GMT+5|<-05>5'>Etc/GMT+5</option>"
                      "<option value='Etc/GMT+6|<-06>6'>Etc/GMT+6</option>"
                      "<option value='Etc/GMT+7|<-07>7'>Etc/GMT+7</option>"
                      "<option value='Etc/GMT+8|<-08>8'>Etc/GMT+8</option>"
                      "<option value='Etc/GMT+9|<-09>9'>Etc/GMT+9</option>"
                      "<option value='Etc/GMT+10|<-10>10'>Etc/GMT+10</option>"
                      "<option value='Etc/GMT+11|<-11>11'>Etc/GMT+11</option>"
                      "<option value='Etc/GMT+12|<-12>12'>Etc/GMT+12</option>"
                      "<option value='Etc/UCT|UTC0'>Etc/UCT</option>"
                      "<option value='Etc/UTC|UTC0'>Etc/UTC</option>"
                      "<option value='Etc/Greenwich|GMT0'>Etc/Greenwich</option>"
                      "<option value='Etc/Universal|UTC0'>Etc/Universal</option>"
                      "<option value='Etc/Zulu|UTC0'>Etc/Zulu</option>";

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();
  delay(10);
  
  Serial.println();
  Serial.println();

  // initialize display
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.display();

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  
  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  
  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  if (!wifiManager.autoConnect((const char *)hostname.c_str())) {// new addition
    delay(3000);
    WiFi.disconnect(true);
    ESP.reset();
    delay(5000);
  }

  //WiFi.begin(WIFI_SSID, WIFI_PWD);

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();

    counter++;
  }

if (ENABLE_OTA) {
    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.setHostname((const char *)hostname.c_str()); 
    if (OTA_Password != "") {
      ArduinoOTA.setPassword(((const char *)OTA_Password.c_str()));
    }
    ArduinoOTA.begin();
  }

  if (WEBSERVER_ENABLED) {
    server.on("/", displayStatus);
    server.on("/systemreset", handleSystemReset);
    server.on("/forgetwifi", handleWifiReset);
    server.on("/updateconfig", handleUpdateConfig);
    server.on("/updateweatherconfig", handleUpdateWeather);
    server.on("/configure", handleConfigure);
    server.on("/configureweather", handleWeatherConfigure);
    server.onNotFound(redirectHome);
    serverUpdater.setup(&server, "/update", www_username, www_password);
    // Start the server
    server.begin();
    Serial.println("Server started");
    // Print the IP address
    String webAddress = "http://" + WiFi.localIP().toString() + ":" + String(WEBSERVER_PORT) + "/";
    Serial.println("Use this URL : " + webAddress);
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 10, "Web Interface On");
    display.drawString(64, 20, "You May Connect to IP");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 30, WiFi.localIP().toString());
    display.drawString(64, 46, "Port: " + String(WEBSERVER_PORT));
    display.display();
    delay(3000);
  } else {
    Serial.println("Web Interface is Disabled");
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 10, "Web Interface is Off");
    display.drawString(64, 20, "Enable in Settings.h");
    display.display(); 
  }
  
  // Get time from network time service
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  ui.setTargetFPS(30);

  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);

  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(BOTTOM);

  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);

  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);

  // Inital UI takes care of initalising the display too.
  ui.init();

  Serial.println("");

  readSettings();
  
  updateData(&display);

}

void loop() {
  // SC - Addition
  unsigned long currentMillis = millis();

  // Check if it's time to update RSSI - SC Addition
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    updateRssi = true;
  }
  else
  {
    updateRssi = false;  
  }
  if (millis() - timeSinceLastWUpdate > (1000L*UPDATE_INTERVAL_SECS)) {
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }

  int remainingTimeBudget = ui.update();

  
  if (WEBSERVER_ENABLED) {
    server.handleClient();
  }
  if (ENABLE_OTA) {
    ArduinoOTA.handle();
  }

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
  }


}

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->flipScreenVertically();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void updateData(OLEDDisplay *display) {
  drawProgress(display, 10, "Updating time...");
  drawProgress(display, 30, "Updating weather...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
  drawProgress(display, 50, "Updating forecasts...");
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);

  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Done...");
  delay(1000);
}



void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];


  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon+1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 5 + y, temp);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}


void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  //String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  //display->drawString(128, 54, temp);
  if (updateRssi == true)
  {
    rssi = WiFi.RSSI();
    Serial.print("Signal strength: ");
    Serial.print(rssi);
    Serial.println("dBm");
  }
    //String rssi_s = String(rssi) + " db";
    //display->drawString(115, 54, rssi_s);

    int bars;
    
    if (rssi > -50) { 
      bars = 4;
    } else if (rssi < -50 & rssi > -60) {
      bars = 3;
    } else if (rssi < -60 & rssi > -70) {
      bars = 2;
    } else if (rssi < -70 & rssi > -80) {
      bars = 1;
    } else {
      bars = 0;
    }



  // Do some simple loop math to draw rectangles as the bars
  // Draw one bar for each "bar" 
    for (int b=0; b <= bars; b++) {
      display->fillRect(114 + (b*3),64 - (b*2),2,b*3); 
    }
  
  
  display->drawHorizontalLine(0, 52, 128);
}

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 0, "Wifi Manager");
  display.drawString(64, 10, "Please connect to AP");
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 26, myWiFiManager->getConfigPortalSSID());
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 46, "To setup Wifi connection");
  display.display();
  
  Serial.println("Wifi Manager");
  Serial.println("Please connect to AP");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.println("To setup Wifi Configuration");
  //flashLED(20, 50);
}

void displayStatus() {

  String html = "";

  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(String(getHeader(true)));

  
 
    if (weatherClient.getCity(0) == "") {
      html += "<p>Please <a href='/configureweather'>Configure Weather</a> API</p>";
      if (weatherClient.getError() != "") {
        html += "<p>Weather Error: <strong>" + weatherClient.getError() + "</strong></p>";
      }
    } else {
      html += "<div class='w3-cell-row' style='width:100%'><h2>" + weatherClient.getCity(0) + ", " + weatherClient.getCountry(0) + "</h2></div><div class='w3-cell-row'>";
      html += "<div class='w3-cell w3-left w3-medium' style='width:120px'>";
      html += "<img src='http://openweathermap.org/img/w/" + weatherClient.getIcon(0) + ".png' alt='" + weatherClient.getDescription(0) + "'><br>";
      html += weatherClient.getHumidity(0) + "% Humidity<br>";
      html += weatherClient.getWind(0) + " <span class='w3-tiny'>" + getSpeedSymbol() + "</span> Wind<br>";
      html += "</div>";
      html += "<div class='w3-cell w3-container' style='width:100%'><p>";
      html += weatherClient.getCondition(0) + " (" + weatherClient.getDescription(0) + ")<br>";
      html += weatherClient.getTempRounded(0) + getTempSymbol(true) + "<br>";
      html += "<a href='https://www.google.com/maps/@" + weatherClient.getLat(0) + "," + weatherClient.getLon(0) + ",10000m/data=!3m1!1e3' target='_BLANK'><i class='fa fa-map-marker' style='color:red'></i> Map It!</a><br>";
      html += "</p></div></div>";
    }
    
    server.sendContent(html); // spit out what we got
    html = ""; // fresh start
 

  server.sendContent(String(getFooter()));
  server.sendContent("");
  server.client().stop();
}

boolean authentication() {
  if (IS_BASIC_AUTH && (strlen(www_username) >= 1 && strlen(www_password) >= 1)) {
    return server.authenticate(www_username, www_password);
  } 
  return true; // Authentication not required
}

void handleSystemReset() {
  if (!authentication()) {
    return server.requestAuthentication();
  }
  Serial.println("Reset System Configuration");
  if (SPIFFS.remove(CONFIG)) {
    redirectHome();
    ESP.restart();
  }
}

void handleUpdateWeather() {
  if (!authentication()) {
    return server.requestAuthentication();
  }
  DISPLAYWEATHER = server.hasArg("isWeatherEnabled");
  WeatherApiKey = server.arg("openWeatherMapApiKey");
  CityIDs[0] = server.arg("city1").toInt();
  IS_METRIC = server.hasArg("metric");
  WeatherLanguage = server.arg("language");
   
  writeSettings();
  updateData(&display);
  redirectHome();
}

void handleUpdateConfig() {
  if (!authentication()) {
    return server.requestAuthentication();
  }
  minutesBetweenDataRefresh = server.arg("refresh").toInt();
  themeColor = server.arg("theme");
  
  TimeZoneLocation = server.arg("timezone");
  TimeZoneValue = TimeZoneValue.substring(TimeZoneLocation.find("|")+1);
  // ,TimeZoneLocation.length()-TimeZoneLocation.find("|"));
  String temp = server.arg("userid");
  temp.toCharArray(www_username, sizeof(temp));
  temp = server.arg("stationpassword");
  temp.toCharArray(www_password, sizeof(temp));
  INVERT_DISPLAY = server.hasArg("invDisp");
  writeSettings();
  updateData(&display);
  redirectHome();
}

void handleWifiReset() {
  if (!authentication()) {
    return server.requestAuthentication();
  }
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  redirectHome();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  ESP.restart();
}

void handleWeatherConfigure() {
  if (!authentication()) {
    return server.requestAuthentication();
  }
  String html = "";

  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  html = getHeader();
  server.sendContent(html);
  
  String form = FPSTR(WEATHER_FORM);
  String isWeatherChecked = "";
  if (DISPLAYWEATHER) {
    isWeatherChecked = "checked='checked'";
  }
  form.replace("%IS_WEATHER_CHECKED%", isWeatherChecked);
  form.replace("%WEATHERKEY%", WeatherApiKey);
  form.replace("%CITYNAME1%", weatherClient.getCity(0));
  form.replace("%CITY1%", String(CityIDs[0]));
  String checked = "";
  if (IS_METRIC) {
    checked = "checked='checked'";
  }
  form.replace("%METRIC%", checked);
  String options = FPSTR(LANG_OPTIONS);
  options.replace(">"+String(WeatherLanguage)+"<", " selected>"+String(WeatherLanguage)+"<");
  form.replace("%LANGUAGEOPTIONS%", options);
  server.sendContent(form);
  
  html = getFooter();
  server.sendContent(html);
  server.sendContent("");
  server.client().stop();
}

void handleConfigure() {
  if (!authentication()) {
    return server.requestAuthentication();
  }
  String html = "";

  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  html = getHeader();
  server.sendContent(html);

  CHANGE_FORM = "<form class='w3-container' action='/updateconfig' method='get'><h2>Weather Station Config:</h2>";

  String form = CHANGE_FORM;
  
  server.sendContent(form);

  form = FPSTR(CLOCK_FORM);
  
 
  String options = "<option>10</option><option>15</option><option>20</option><option>30</option><option>60</option>";
  options.replace(">"+String(minutesBetweenDataRefresh)+"<", " selected>"+String(minutesBetweenDataRefresh)+"<");
  form.replace("%OPTIONS%", options);

  server.sendContent(form);

  form = FPSTR(THEME_FORM);
  
  String themeOptions = FPSTR(COLOR_THEMES);
  themeOptions.replace(">"+String(themeColor)+"<", " selected>"+String(themeColor)+"<");
  form.replace("%THEME_OPTIONS%", themeOptions);

  String timeZoneOptions = FPTSR(TIME_ZONES);
  timeZoneOptions.replace(">"+String(TimeZoneLocation)+"<", " selected>"+String(TimeZoneLocation)+"<");
  form.replace("%TIME_ZONES%", timeZoneOptions);
  
  form.replace("%UTCOFFSET%", String(UtcOffset));
  String isUseSecurityChecked = "";
  if (IS_BASIC_AUTH) {
    isUseSecurityChecked = "checked='checked'";
  }
  form.replace("%IS_BASICAUTH_CHECKED%", isUseSecurityChecked);
  form.replace("%USERID%", String(www_username));
  form.replace("%STATIONPASSWORD%", String(www_password));

  server.sendContent(form);
  
  html = getFooter();
  server.sendContent(html);
  server.sendContent("");
  server.client().stop();
}
void displayMessage(String message) {
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  String html = getHeader();
  server.sendContent(String(html));
  server.sendContent(String(message));
  html = getFooter();
  server.sendContent(String(html));
  server.sendContent("");
  server.client().stop();
}

void redirectHome() {
  // Send them back to the Root Directory
  server.sendHeader("Location", String("/"), true);
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");
  server.client().stop();
}

String getHeader() {
  return getHeader(false);
}

String getHeader(boolean refresh) {
  String menu = FPSTR(WEB_ACTIONS);

  String html = "<!DOCTYPE HTML>";
  html += "<html><head><title>Weather Station</title><link rel='icon' href='data:;base64,='>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  if (refresh) {
    html += "<meta http-equiv=\"refresh\" content=\"30\">";
  }
  html += "<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/lib/w3-theme-" + themeColor + ".css'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css'>";
  html += "</head><body>";
  html += "<nav class='w3-sidebar w3-bar-block w3-card' style='margin-top:88px' id='mySidebar'>";
  html += "<div class='w3-container w3-theme-d2'>";
  html += "<span onclick='closeSidebar()' class='w3-button w3-display-topright w3-large'><i class='fa fa-times'></i></span>";
  html += "<div class='w3-cell w3-left w3-xxxlarge' style='width:60px'><i class='fa fa-cube'></i></div>";
  html += "<div class='w3-padding'>Menu</div></div>";
  html += menu;
  html += "</nav>";
  html += "<header class='w3-top w3-bar w3-theme'><button class='w3-bar-item w3-button w3-xxxlarge w3-hover-theme' onclick='openSidebar()'><i class='fa fa-bars'></i></button><h2 class='w3-bar-item'>Weather Station</h2></header>";
  html += "<script>";
  html += "function openSidebar(){document.getElementById('mySidebar').style.display='block'}function closeSidebar(){document.getElementById('mySidebar').style.display='none'}closeSidebar();";
  html += "</script>";
  html += "<br><div class='w3-container w3-large' style='margin-top:88px'>";
  return html;
}

String getFooter() {
  int8_t rssi = WiFi.RSSI();
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(rssi);
  Serial.println("%");
  String html = "<br><br><br>";
  html += "</div>";
  html += "<footer class='w3-container w3-bottom w3-theme w3-margin-top'>";
    html += "<i class='fa fa-rss'></i> Signal Strength: ";
  html += String(rssi) + "db";
  html += "</footer>";
  html += "</body></html>";
  return html;
}

void writeSettings() {
  // Save decoded message to SPIFFS file for playback on power up.
  File f = SPIFFS.open(CONFIG, "w");
  if (!f) {
    Serial.println("File open failed!");
  } else {
    Serial.println("Saving settings now...");
    f.println("refreshRate=" + String(minutesBetweenDataRefresh));
    f.println("themeColor=" + themeColor);
    f.println("timeZoneLocation=" + TimeZoneLocation);
    f.println("timeZoneValue=" + TimeZoneValue);
    f.println("IS_BASIC_AUTH=" + String(IS_BASIC_AUTH));
    f.println("www_username=" + String(www_username));
    f.println("www_password=" + String(www_password));
    f.println("is24hour=" + String(IS_24HOUR));
    f.println("invertDisp=" + String(INVERT_DISPLAY));
    f.println("isWeather=" + String(DISPLAYWEATHER));
    f.println("weatherKey=" + WeatherApiKey);
    f.println("CityID=" + String(CityIDs[0]));
    f.println("isMetric=" + String(IS_METRIC));
    f.println("language=" + String(WeatherLanguage));
  }
  f.close();
  readSettings();
}

void readSettings() {
  if (SPIFFS.exists(CONFIG) == false) {
    Serial.println("Settings File does not yet exists.");
    writeSettings();
    return;
  }
  File fr = SPIFFS.open(CONFIG, "r");
  String line;
  while(fr.available()) {
    line = fr.readStringUntil('\n');

    if (line.indexOf("refreshRate=") >= 0) {
      minutesBetweenDataRefresh = line.substring(line.lastIndexOf("refreshRate=") + 12).toInt();
      Serial.println("minutesBetweenDataRefresh=" + String(minutesBetweenDataRefresh));
    }
    if (line.indexOf("timeZoneLocation=") >= 0) {
      TimeZoneLocation = line.substring(line.lastIndexOf("timeZoneLocation=") + 17);
      TimeZoneLocation.trim();
      Serial.println("timeZoneLocation=" + TimeZoneLocation);
    }
    if (line.indexOf("timeZoneValue=") >= 0) {
      TimeZoneValue = line.substring(line.lastIndexOf("timeZoneValue=") + 14);
      TimeZoneValue.trim();
      Serial.println("timeZoneValue=" + TimeZoneValue);
    }
    if (line.indexOf("themeColor=") >= 0) {
      themeColor = line.substring(line.lastIndexOf("themeColor=") + 11);
      themeColor.trim();
      Serial.println("themeColor=" + themeColor);
    }
    if (line.indexOf("IS_BASIC_AUTH=") >= 0) {
      IS_BASIC_AUTH = line.substring(line.lastIndexOf("IS_BASIC_AUTH=") + 14).toInt();
      Serial.println("IS_BASIC_AUTH=" + String(IS_BASIC_AUTH));
    }
    if (line.indexOf("www_username=") >= 0) {
      String temp = line.substring(line.lastIndexOf("www_username=") + 13);
      temp.trim();
      temp.toCharArray(www_username, sizeof(temp));
      Serial.println("www_username=" + String(www_username));
    }
    if (line.indexOf("www_password=") >= 0) {
      String temp = line.substring(line.lastIndexOf("www_password=") + 13);
      temp.trim();
      temp.toCharArray(www_password, sizeof(temp));
      Serial.println("www_password=" + String(www_password));
    }
    if (line.indexOf("is24hour=") >= 0) {
      IS_24HOUR = line.substring(line.lastIndexOf("is24hour=") + 9).toInt();
      Serial.println("IS_24HOUR=" + String(IS_24HOUR));
    }
    if(line.indexOf("invertDisp=") >= 0) {
      INVERT_DISPLAY = line.substring(line.lastIndexOf("invertDisp=") + 11).toInt();
      Serial.println("INVERT_DISPLAY=" + String(INVERT_DISPLAY));
    }
    if (line.indexOf("isWeather=") >= 0) {
      DISPLAYWEATHER = line.substring(line.lastIndexOf("isWeather=") + 10).toInt();
      Serial.println("DISPLAYWEATHER=" + String(DISPLAYWEATHER));
    }
    if (line.indexOf("weatherKey=") >= 0) {
      WeatherApiKey = line.substring(line.lastIndexOf("weatherKey=") + 11);
      WeatherApiKey.trim();
      Serial.println("WeatherApiKey=" + WeatherApiKey);
    }
    if (line.indexOf("CityID=") >= 0) {
      CityIDs[0] = line.substring(line.lastIndexOf("CityID=") + 7).toInt();
      Serial.println("CityID: " + String(CityIDs[0]));
    }
    if (line.indexOf("isMetric=") >= 0) {
      IS_METRIC = line.substring(line.lastIndexOf("isMetric=") + 9).toInt();
      Serial.println("IS_METRIC=" + String(IS_METRIC));
    }
    if (line.indexOf("language=") >= 0) {
      WeatherLanguage = line.substring(line.lastIndexOf("language=") + 9);
      WeatherLanguage.trim();
      Serial.println("WeatherLanguage=" + WeatherLanguage);
    }
  }
  fr.close();
  OPEN_WEATHER_MAP_APP_ID = WeatherApiKey;
  OPEN_WEATHER_MAP_LANGUAGE = WeatherLanguage;
  OPEN_WEATHER_MAP_LOCATION_ID = CityIDs[0];
}
String getTempSymbol() {
  return getTempSymbol(false);
}

String getTempSymbol(boolean forHTML) {
  String rtnValue = "F";
  if (IS_METRIC) {
    rtnValue = "C";
  }
  if (forHTML) {
    rtnValue = "&#176;" + rtnValue;
  } else {
    rtnValue = "°" + rtnValue;
  }
  return rtnValue;
}

String getSpeedSymbol() {
  String rtnValue = "mph";
  if (IS_METRIC) {
    rtnValue = "kph";
  }
  return rtnValue;
}

String zeroPad(int value) {
  String rtnValue = String(value);
  if (value < 10) {
    rtnValue = "0" + rtnValue;
  }
  return rtnValue;
}
