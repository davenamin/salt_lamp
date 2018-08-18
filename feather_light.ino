/**
   sketch for the salt lamp light clock.
   Daven Amin, 01/06/2018

   uses code from:
   https://www.arduino.cc/en/Tutorial/ConnectWithWPA

   and the idea for updating time from the web came from:
   http://playground.arduino.cc//Code/Webclient

   also uses FastLED, ArduinoJson and Adafruit's fork of RTClib:
   https://github.com/FastLED/FastLED
   https://github.com/bblanchon/ArduinoJson
   https://github.com/adafruit/RTClib

   and is powered by Dark Sky:
   https://darksky.net/poweredby/
*/

#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <ArduinoJson.h>

// defines for WIFI_SSID, WIFI_PASS, and DARKSKY_API_KEY
#include "keys.h"

#define LED_PIN     13
#define COLOR_ORDER GRB
#define CHIPSET     WS2812B
#define NUM_LEDS    30

#define UPDATE_RATE 1000

// LCG RNG parameters
// (A = LCF of primes of NUM_LEDS + 1,
//  C = relative prime of NUM_LEDS)
#define LCG_A 31
#define LCG_C 11

const char* ssid = WIFI_SSID;
const char* pass = WIFI_PASS;

const String weather_api_key = DARKSKY_API_KEY;

RTC_DS3231 rtc;
CRGB leds[NUM_LEDS];

float latitude = 42.4154;  // set to a default
float longitude = -71.1564;  // set to a default

enum WEATHER_TYPE {unknown, clear_day, clear_night,
                   rain, snow, sleet,
                   wind, fog, cloudy,
                   partly_cloudy_day, partly_cloudy_night
                  };
WEATHER_TYPE weather = unknown; // set to a default

// used for palettes - give each LED a chunk of the palette to walk
int palette_stride = 255 / NUM_LEDS;

void setup() {
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  Serial.begin(9600);
  rtc.begin();
  Serial.setDebugOutput(true);
  WiFi.setAutoConnect(false);
  startWifi();
  updateTime();
  updateLocation();
  stopWifi();
}

void startWifi() {
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  // Connect to WPA/WPA2 network:
  WiFi.begin(ssid, pass);
  WiFi.mode(WIFI_STA);
  // attempt to connect to Wifi network:
  unsigned long watchdog_start = millis();
  unsigned long watchdog = 0;
  // try to connect for 10 seconds
  while ( watchdog < 10000) {
    delay(500);
    Serial.print(".");
    watchdog = abs(millis() - watchdog_start);
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("Connected!");
      break;
    }
  }
  WiFi.printDiag(Serial);
}
void stopWifi() {
  WiFi.disconnect(true);
  delay(500);
}

const char* time_service = "time.nist.gov";
const int time_port = 13;
void updateTime() {
  WiFiClient client;
  TimeSpan tz_offset = TimeSpan(0, 5, 0, 0);
  // set the initial time if we can
  if (client.connect(time_service, time_port))
  {
    String datetime = client.readString();
    Serial.println("NIST time follows:");
    Serial.println(datetime);
    Serial.println("Parsing NIST time");
    if (datetime.length() >= 25)
    {
      // NIST format: JJJJJ YR-MO-DA HH:MM:SS TT L H msADV UTC(NIST) OTM
      // where YR-MO-DA HH:MM:SS are parsed below
      // looks like the datetime string has added linefeeds on the start and end...
      String year = datetime.substring(7, 9);
      String month = datetime.substring(10, 12);
      String day = datetime.substring(13, 15);
      String hour = datetime.substring(16, 18);
      String minute = datetime.substring(19, 21);
      String second = datetime.substring(22, 24);
      Serial.print("year: " + year);
      Serial.print("  month: " + month);
      Serial.print("  day: " + day);
      Serial.print("time: " + hour + ":" + minute + ":" + second);
      Serial.println();

      rtc.adjust(DateTime(year.toInt(), month.toInt(), day.toInt(),
                          hour.toInt(), minute.toInt(), second.toInt()) - tz_offset);
      Serial.println("Set time!");
    }
    client.stop();
  }
  else {
    Serial.println("couldn't connect to time service");
  }
}

void displayTime() {
  DateTime now = rtc.now();

  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.day(), DEC);
  Serial.print(" (DayOfWeek: ");
  Serial.print(now.dayOfTheWeek(), DEC);
  Serial.print(") ");
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.println();
}


const String location_service = "ip-api.com";
const int location_port = 80;
void updateLocation() {
  WiFiClient client;

  // set the location, if we can
  if (client.connect(location_service, location_port))
  {
    Serial.println("Trying to set location");
    // try to get the location
    client.println("GET /json HTTP/1.0");
    client.println("Host: " + location_service);
    client.println("Connection: close");
    client.println();

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    Serial.print("Status arrived as: ");
    Serial.println(status);
    char endOfHeaders[] = "\r\n\r\n";
    client.find(endOfHeaders);

    String location_json = client.readString();
    Serial.println(location_json);

    DynamicJsonBuffer locationBuffer;
    JsonObject& root = locationBuffer.parseObject(location_json);

    if (root.success())
    {
      latitude = root["lat"];
      longitude = root["lon"];
      Serial.println("set location to: " + String(latitude) + "," + String(longitude));
    }
    client.stop();
  }
  else {
    Serial.println("couldn't connect to location service");
  }
}

const String weather_service = "api.darksky.net";
const int weather_port = 443;
const String weather_params = "?exclude=currently,hourly,daily,alerts,flags";

void updateWeather() {
  WiFiClientSecure client;

  if (client.connect(weather_service, weather_port))
  {
    Serial.println("Trying to check weather");
    // try to get the location
    client.println("GET /forecast/" + weather_api_key +
                   "/" + latitude +
                   "," + longitude +
                   weather_params +
                   " HTTP/1.0");
    client.println("Host: " + weather_service);
    client.println("Connection: close");
    client.println();

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    Serial.print("Status arrived as: ");
    Serial.println(status);
    char endOfHeaders[] = "\r\n\r\n";
    client.find(endOfHeaders);

    String weather_json = client.readString();

    DynamicJsonBuffer weatherBuffer;
    JsonObject& root = weatherBuffer.parseObject(weather_json);

    if (root.success())
    {
      JsonObject& weather_data = root["minutely"];
      String current_weather = weather_data["icon"];
      Serial.println("parsed weather as: " + current_weather);

      //{{ String current_weather = "rain";
      if (current_weather.equalsIgnoreCase("clear-day")) {
        weather = clear_day;
      }
      else if (current_weather.equalsIgnoreCase("clear-night")) {
        weather = clear_night;
      }
      else if (current_weather.equalsIgnoreCase("rain")) {
        weather = rain;
      }
      else if (current_weather.equalsIgnoreCase("snow")) {
        weather = snow;
      }
      else if (current_weather.equalsIgnoreCase("sleet")) {
        weather = sleet;
      }
      else if (current_weather.equalsIgnoreCase("wind")) {
        weather = wind;
      }
      else if (current_weather.equalsIgnoreCase("fog")) {
        weather = fog;
      }
      else if (current_weather.equalsIgnoreCase("cloudy")) {
        weather = cloudy;
      }
      else if (current_weather.equalsIgnoreCase("partly-cloudy-day")) {
        weather = partly_cloudy_day;
      }
      else if (current_weather.equalsIgnoreCase("partly-cloudy-night")) {
        weather = partly_cloudy_night;
      }
      else {
        weather = unknown;
      }
      Serial.println( "set weather to: " + String(weather));
    }
    client.stop();
  }
  else {
    Serial.println("couldn't connect to weather service");
  }
}

// fallback
void standard_color() {
  for (int dot = 0; dot < NUM_LEDS; dot++) {
    leds[dot] = CRGB::White;
    FastLED.show();
    delay(UPDATE_RATE / NUM_LEDS);
  }
}



void apply_colors(CRGBPalette16 palette) {

  // this is "position on the strip" so that it's not sequential
  int dot = 0;

  // are we walking up or down the palette? (want to go back and forth)
  bool walkingUp = true;
  int paletteix = 0;
  int ledix = 0;

  //(for each palette section, update all LEDs)
  while (paletteix >= 0)
  {
    // compute and set color
    int palette_offset = (ledix*palette_stride) + paletteix;
    leds[dot] = ColorFromPalette(palette, palette_offset);
    // compute next position on the strip via LCG
    dot = (LCG_A * dot + LCG_C) % NUM_LEDS;
    FastLED.show();
    delay(UPDATE_RATE / NUM_LEDS);

    // increment ledix and loop around
    ledix++;
    if (ledix >= NUM_LEDS)
    {
      // restart led counter
      ledix = 0;
      // increment or decrement paletteix
      if (!walkingUp)
      {
        paletteix--;
      } else if (paletteix >= palette_stride) {
        walkingUp = false; // switch directions
      } else {
        paletteix++;
      }
    }
  }
}

// clear
void steady_blue() {
  CRGBPalette16 clearPalette = ForestColors_p;
  apply_colors(clearPalette);
}

// cloudy
void flashing_blue() {
  CRGBPalette16 cloudyPalette = OceanColors_p;
  apply_colors(cloudyPalette);
}

// rain
void steady_red() {
  CRGBPalette16 rainPalette = PartyColors_p;
  apply_colors(rainPalette);
}

// snow
void flashing_red() {
  CRGBPalette16 snowPalette = HeatColors_p;
  apply_colors(snowPalette);
}

long next_update = 0;
const long update_interval = 15 * 60; // seconds
void loop() {
  // do we need to check the weather?
  long now = rtc.now().secondstime();
  if (now > next_update)
  {
    Serial.println("need to check for weather update");
    startWifi();
    updateWeather();
    stopWifi();
    next_update = now + update_interval;
  }
  displayTime();

  switch (weather) {
    case clear_day:
    case clear_night:
    case wind: {
        steady_blue();
        break;
      }
    case fog:
    case cloudy:
    case partly_cloudy_day:
    case partly_cloudy_night: {
        flashing_blue();
        break;
      }
    case rain: {
        steady_red();
        break;
      }
    case snow:
    case sleet: {
        flashing_red();
        break;
      }
    default: {  // unknown
        standard_color();
        break;
      }
  }
}
