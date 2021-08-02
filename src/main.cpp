/****************************************************************************************************************************
  Wifi Credentials epaper totem
  For LilyGo T5_V2.3.1 2.13 (DEPG0213BN)

  Uses:
  ESPAsync_WiFiManager by Khoi Hoang https://github.com/khoih-prog/ESPAsync_WiFiManager (MIT License)
  ESP_DoubleResetDetector library from //https://github.com/khoih-prog/ESP_DoubleResetDetector (MIT?) (Open a configuration portal when the reset button is pressed twice.)
  ArduinoJson library https://arduinojson.org/ 
  GxEPD library fork https://github.com/lewisxhe/GxEPD
  u8g2 library https://github.com/olikraus/u8g2
  QRCode library  https://github.com/yoprogramo/ESP_QRcode
  
 *****************************************************************************************************************************/

#define ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET     "ESPAsync_WiFiManager v1.9.1"

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _ESPASYNC_WIFIMGR_LOGLEVEL_    1

#include <FS.h>

// Now support ArduinoJson 6.0.0+ ( tested with v6.15.2 to v6.16.1 )
#include <ArduinoJson.h>        // get it from https://arduinojson.org/ or install via Arduino library manager

#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiMulti.h>
#include <SPIFFS.h>        

#define LILYGO_T5_V213

#include <boards.h>
#include <GxEPD.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h> 
#include <U8g2_for_Adafruit_GFX.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <Wire.h>
#include <qrcode.h>

#define LILYGO_T5_V213
#define USE_LITTLEFS    false
#define USE_SPIFFS      true

#define FileFS        SPIFFS
#define FS_Name       "SPIFFS"
#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())

WiFiMulti wifiMulti;
FS* filesystem =      &SPIFFS;
GxIO_Class io(SPI,  EPD_CS, EPD_DC,  EPD_RSET);
GxEPD_Class display(io, EPD_RSET, EPD_BUSY);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

QRcode qrcode (&display);

// These defines must be put before #include <ESP_DoubleResetDetector.h>
// to select where to store DoubleResetDetector's variable.
// For ESP32, You must select one to be true (EEPROM or SPIFFS)
// For ESP8266, You must select one to be true (RTC, EEPROM, SPIFFS or LITTLEFS)
// Otherwise, library will use default EEPROM storage
// These defines must be put before #include <ESP_DoubleResetDetector.h>
// to select where to store DoubleResetDetector's variable.
// For ESP32, You must select one to be true (EEPROM or SPIFFS)
// Otherwise, library will use default EEPROM storage
#define ESP_DRD_USE_SPIFFS      true

#define DOUBLERESETDETECTOR_DEBUG       true  //false

#include <ESP_DoubleResetDetector.h>      //https://github.com/khoih-prog/ESP_DoubleResetDetector

// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

//DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);
DoubleResetDetector* drd;//////

const char* CONFIG_FILE = "/ConfigCREDENTIALS.json";

// Default configuration values for Credentials server

#define CREDENTIALS_JSON  "http://192.168.1.1/guest_credentials.json"
#define INFO1_REST        "http://192.168.1.100:8080/rest/items/Piscina_TemperaturaVerificada"
#define INFO2_REST        "http://192.168.1.100:8080/rest/items/Piscina_Temperatura_Verificada_Update"               

// Labels for custom parameters in WiFi manager
#define CREDENTIALS_JSON_Label             "Credentials JSON Call URI"
#define INFO1_REST_Label        "Info 1 Rest Call URI"
#define INFO2_REST_Label        "Info 2 Rest Call URI"

// Variables to save custom parameters to...
// I would like to use these instead of #defines
#define custom_CREDENTIALS_JSON_LEN   64
#define custom_INFO1_REST_LEN         96
#define custom_INFO2_REST_LEN         96

char custom_CREDENTIALS_JSON[custom_CREDENTIALS_JSON_LEN];
char custom_INFO1_REST[custom_INFO1_REST_LEN];
char custom_INFO2_REST[custom_INFO2_REST_LEN];

String ssid = "ESP_" + String(ESP_getChipId(), HEX);
String password;

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false

#define MIN_AP_PASSWORD_SIZE    8

#define SSID_MAX_LEN            32
#define PASS_MAX_LEN            64

typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct
{
  String wifi_ssid;
  String wifi_pw;
}  WiFi_Credentials_String;

#define NUM_WIFI_CREDENTIALS      2

// Assuming max 49 chars
#define TZNAME_MAX_LEN            50
#define TIMEZONE_MAX_LEN          50

// Amount of time to transform wifi credentials creation date/time to expire date/time.
// Adjust accordingly to struct tm properties. Google time.h
#define CREATION_TO_EXPIRE        tm_mday +=1

typedef struct
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
  char TZ_Name[TZNAME_MAX_LEN];     // "America/Toronto"
  char TZ[TIMEZONE_MAX_LEN];        // "EST5EDT,M3.2.0,M11.1.0"
  uint16_t checksum;
} WM_Config;

WM_Config         WM_config;

#define  CONFIG_FILENAME              F("/wifi_cred.dat")

// Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
bool initialConfig = false;

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <ESPAsync_WiFiManager.h>
#define USE_AVAILABLE_PAGES     false

// From v1.0.10 to permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
// You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
// You have to explicitly specify false to disable the feature.
#define USE_STATIC_IP_CONFIG_IN_CP          false

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_WIFIMANAGER_NTP     true

// Just use enough to save memory. On ESP8266, can cause blank ConfigPortal screen
// if using too much memory
#define USING_AFRICA        false
#define USING_AMERICA       true
#define USING_ANTARCTICA    false
#define USING_ASIA          false
#define USING_ATLANTIC      false
#define USING_AUSTRALIA     false
#define USING_EUROPE        false
#define USING_INDIAN        false
#define USING_PACIFIC       false
#define USING_ETC_GMT       false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP          false

// New in v1.0.11
#define USING_CORS_FEATURE          true
//////

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
// Force DHCP to be true
#if defined(USE_DHCP_IP)
#undef USE_DHCP_IP
#endif
#define USE_DHCP_IP     true
#else
// You can select DHCP or Static IP here
#define USE_DHCP_IP     true
//#define USE_DHCP_IP     false
#endif

IPAddress stationIP   = IPAddress(0, 0, 0, 0);
IPAddress gatewayIP   = IPAddress(192, 168, 1, 1);
IPAddress netMask     = IPAddress(255, 255, 255, 0);

#define USE_CONFIGURABLE_DNS      true

IPAddress dns2IP      = gatewayIP;
IPAddress dns1IP      = IPAddress(192, 168, 1, 100);

#define USE_CUSTOM_AP_IP          false

#include <ESPAsync_WiFiManager.h>    

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig)
{
  in_WM_AP_IPconfig._ap_static_ip   = APStaticIP;
  in_WM_AP_IPconfig._ap_static_gw   = APStaticGW;
  in_WM_AP_IPconfig._ap_static_sn   = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
  in_WM_STA_IPconfig._sta_static_ip   = stationIP;
  in_WM_STA_IPconfig._sta_static_gw   = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn   = netMask;
#if USE_CONFIGURABLE_DNS
  in_WM_STA_IPconfig._sta_static_dns1 = dns1IP;
  in_WM_STA_IPconfig._sta_static_dns2 = dns2IP;
#endif
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
  LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, ", gatewayIP =", in_WM_STA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
#if USE_CONFIGURABLE_DNS
  LOGERROR3(F("dns1IP ="), in_WM_STA_IPconfig._sta_static_dns1, ", dns2IP =", in_WM_STA_IPconfig._sta_static_dns2);
#endif
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
#if USE_CONFIGURABLE_DNS
  // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn, in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);
#else
  // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn);
#endif
}

///////////////////////////////////////////

uint8_t connectMultiWiFi()
{
#if ESP32
  // For ESP32, this better be 0 to shorten the connect time.
  // For ESP32-S2/C3, must be > 500
  #if ( USING_ESP32_S2 || USING_ESP32_C3 )
    #define WIFI_MULTI_1ST_CONNECT_WAITING_MS           500L
  #else
    // For ESP32 core v1.0.6, must be >= 500
    #define WIFI_MULTI_1ST_CONNECT_WAITING_MS           800L
  #endif
#else
  // For ESP8266, this better be 2200 to enable connect the 1st time
  #define WIFI_MULTI_1ST_CONNECT_WAITING_MS             2200L
#endif

#define WIFI_MULTI_CONNECT_WAITING_MS                   500L

  uint8_t status;

  WiFi.mode(WIFI_STA);

  LOGERROR(F("ConnectMultiWiFi with :"));

  if ( (Router_SSID != "") && (Router_Pass != "") )
  {
    LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass );
    LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass );
    wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
  }

  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
  {
    // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
    if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
    {
      LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
    }
  }

  LOGERROR(F("Connecting MultiWifi..."));

#if !USE_DHCP_IP
  // New in v1.4.0
  configWiFi(WM_STA_IPconfig);
  //////
#endif

  int i = 0;
  status = wifiMulti.run();
  delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

  while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) )
  {
    status = wifiMulti.run();

    if ( status == WL_CONNECTED )
      break;
    else
      delay(WIFI_MULTI_CONNECT_WAITING_MS);
  }

  if ( status == WL_CONNECTED )
  {
    LOGERROR1(F("WiFi connected after time: "), i);
    LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
    LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
  }
  else
  {
    LOGERROR(F("WiFi not connected"));

    // To avoid unnecessary DRD
    drd->loop();
  
    ESP.restart();
  }

  return status;
}

#if USE_ESP_WIFIMANAGER_NTP

void printLocalTime()
{
  struct tm timeinfo;

  getLocalTime( &timeinfo );

  // Valid only if year > 2000. 
  // You can get from timeinfo : tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec
  if (timeinfo.tm_year > 100 )
  {
    Serial.print("Local Date/Time: ");
    Serial.print( asctime( &timeinfo ) );
  }
}
#endif

void heartBeatPrint()
{
#if USE_ESP_WIFIMANAGER_NTP
  printLocalTime();
#else
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("H"));        // H means connected to WiFi
  else
    Serial.print(F("F"));        // F means not connected to WiFi

  if (num == 80)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0)
  {
    Serial.print(F(" "));
  }
#endif  
}

void check_WiFi()
{
  if ( (WiFi.status() != WL_CONNECTED) )
  {
    Serial.println(F("\nWiFi lost. Call connectMultiWiFi in loop"));
    connectMultiWiFi();
  }
}

void drawLineMessage(const uint8_t* icon_font, const char* icon, const uint16_t message_offset, const char* line1, 
                     const uint16_t line1_width, const char* line2, const uint16_t line2_width, const uint16_t y, const uint16_t height)
// drawLineMessage overloaded for two lines of information in one block.
// see overloaded funcion below
{
  uint16_t width = display.width();
  
  display.fillRect(0, y , width, height, GxEPD_BLACK);
  display.updateWindow(0, y, width, height, false);
  display.fillRect(0, y , width, height, GxEPD_WHITE);
  display.updateWindow(0, y, width, height, false);
 
  u8g2Fonts.setFont(icon_font);
  u8g2Fonts.drawStr(0, y+height, icon);

  u8g2Fonts.setFont(u8g2_font_helvB08_tf); // has to be a small font. 8px for a 23~25 pix height field
  uint16_t x = (width - line1_width - message_offset)/2 + message_offset; //line 1
  u8g2Fonts.drawStr(x, y+height-10, line1);
  x = (width - line2_width - message_offset) /2 + message_offset; //line 2
  u8g2Fonts.drawStr(x, y+height, line2); 

  display.updateWindow(0, y, width, height, false);
}

void drawLineMessage(const uint8_t* icon_font, const char* icon, const uint16_t message_offset, const char* line, 
                     const uint16_t line_width, const uint16_t y, const uint16_t height, const uint16_t offsetY_message)
// drawLineMessage overloaded for onr line of information in one block.
// see overloaded funcion above.
{
  uint16_t width = display.width();
  
  display.fillRect(0, y , width, height, GxEPD_BLACK);
  display.updateWindow(0, y, width, height, false);
  display.fillRect(0, y , width, height, GxEPD_WHITE);
  display.updateWindow(0, y, width, height, false);
 
  u8g2Fonts.setFont(icon_font);
  u8g2Fonts.drawStr(0, y+height, icon);

  u8g2Fonts.setFont(u8g2_font_helvB12_tf);  // a 12px font is good for a 25 pix height field with enought room for offsetY
  uint16_t x = (width - line_width - message_offset)/2 + message_offset;
  u8g2Fonts.drawStr(x, y+height-offsetY_message, line); // single line of text

  display.updateWindow(0, y, width, height, false);
}

void drawCredentials(const char* guest_ssid, const char* guest_password, const char* expire_tm)
{
  uint16_t width = display.width();
  uint16_t height = display.height();

  display.fillRect(0, 0 , width, height-75, GxEPD_BLACK);
  display.updateWindow(0, 0, width, height-75, false);
  display.fillRect(0, 0 , width, height-75, GxEPD_WHITE);
  display.updateWindow(0, 0, width, height-75, false);

  char message [64]; 

  strcpy(message,"WIFI:T:WPA;S:");
  strcat(message, guest_ssid);
  strcat(message, ";P:");
  strcat(message, guest_password);
  strcat(message, ";;");
  
  qrcode.create(message);  // draws qrcode. See https://github.com/yoprogramo/ESP_QRcode. 

  display.updateWindow(0, 0, width, height-75, false);

  u8g2Fonts.setFont(u8g2_font_helvB12_tf); // just to get the width with u8g2 getUTF8Width.
  
  drawLineMessage(u8g2_font_open_iconic_www_2x_t, "\x51", 16, guest_ssid, 
                  u8g2Fonts.getUTF8Width(guest_ssid), height - 148, 25, 3); //single info block with SSID
   
  drawLineMessage(u8g2_font_open_iconic_thing_2x_t, "\x4F", 16, guest_password, 
                  u8g2Fonts.getUTF8Width(guest_password), height - 123, 25, 4); //single info block with password
  
  u8g2Fonts.setFont(u8g2_font_helvB08_tf); // just to get the width with u8g2 get UTF8Width
 
  drawLineMessage(u8g2_font_open_iconic_app_2x_t, "\x42", 16, "V\xe1lido at\xe9", 
                  u8g2Fonts.getUTF8Width("Válido até"), expire_tm, u8g2Fonts.getUTF8Width(expire_tm), height - 98, 23);
    // two lines of text in one block.
}

void updateCredentials()
  //obtain credentials and update information IF REQUIRED ///////////////
{
  // Obtains credentials from CREDENTIALS JSON FILE
  // Not an API call, simple file download.
  HTTPClient http; // Connect to HTTP server

  // Send request
  http.useHTTP10(true);
  http.begin(custom_CREDENTIALS_JSON);
  http.GET();

  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<192> doc;

  // Parse JSON object
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end(); // Disconnect

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    drawCredentials("N/A", "N/A", "N/A");
    return;
  }
  http.end(); // Disconnect

  const char* guest_ssid = doc["guest_ssid"]; 
  const char* guest_password = doc["guest_password"];
  const char* creation_date = doc["creation_date"]; 
  static char saved_date[32] = "YYYY-MM-DDT00:00:00Z";

 if (strcmp(creation_date, saved_date) == 0)
     return;

  strcpy(saved_date,creation_date); //keeping last creation date saved between function calls

  struct tm valid_tm = {0}; 
  time_t      stamp;
  char buf[16];
  
  // Convert to tm struct
  strptime(creation_date, "%Y-%m-%dT%H:%M:%SZ", &valid_tm); //get creation_date string into a tm struct for easy of parsing.
  valid_tm.CREATION_TO_EXPIRE; //valid_tm is the time when credentials will expire. Adjust CREATION_TO_EXPIRE definition accordingly.

  stamp = mktime(&valid_tm); // obtain time stamp as epoch time.
  strftime(buf, sizeof(buf), "%H:%M de %d/%m", gmtime(&stamp)); //    Transforms tm from UTC to Local and format tm into a time data string. Change accordingly to the info you want to display.
  drawCredentials(guest_ssid, guest_password, buf);  
}

void additionalInfo()
// contents of the first information block.
{
  uint16_t height = display.height();

  HTTPClient http; // Connect to HTTP server

  // Send request
 // http.useHTTP10(true);

  http.begin(custom_INFO2_REST);
  http.addHeader("Accept", "application/json");
  http.GET();
  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<768> doc;
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  const char* state2 = doc["state"]; 
  static char saved_date[32] = "YYYY-MM-DDT00:00:00Z";

  if (strcmp(state2, saved_date) == 0)
     return;
  
  strcpy(saved_date,state2); //keeping last creation date saved between function calls

  struct tm valid_tm = {0}; 
  char buf2[32];
 
  // Convert to tm struct
  strptime(state2, "%Y-%m-%dT%H:%M:%SZ", &valid_tm); //get creation_date string into a tm struct for easy of parsing.
  strftime(buf2, sizeof(buf2), "Em %H:%M de %d/%m", &valid_tm); //    Transforms tm from UTC to Local and format tm into a time data string. Change accordingly to the info you want to display.

  http.begin(custom_INFO1_REST);
  http.addHeader("Accept", "application/json");
  http.GET();

  //StaticJsonDocument<768> doc;
  error = deserializeJson(doc, http.getString());
  http.end();
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char* state1 = doc["state"]; // "25.125 °C"
  char vstate[16];
  strcpy(vstate, state1);

  char buf[32];
  strcpy(buf, "Piscina: ");

  char* token;
  const char* delim = "\xc2";
  token = strtok(vstate, delim);
  while (token != NULL){
    strcat(buf, token);
    token = strtok(NULL, delim);
  }


  

  u8g2Fonts.setFont(u8g2_font_helvB08_tf);  // just to get correct message width
  drawLineMessage(u8g2_font_open_iconic_weather_2x_t, "\x45", 16, buf, 
                  u8g2Fonts.getUTF8Width(buf), buf2, u8g2Fonts.getUTF8Width(buf2), height - 75, 25);
}

void additionalInfo2()
// contents of the second information block.
{
  uint16_t height = display.height();
  
  char buf[32];
  struct tm timeinfo;

  getLocalTime( &timeinfo );
  strftime(buf, sizeof(buf), "Em %H:%M de %d/%m", &timeinfo);

  u8g2Fonts.setFont(u8g2_font_helvB08_tf);
  drawLineMessage(u8g2_font_open_iconic_app_2x_t, "\x45", 16, "\xdaltimo update:", 
                  u8g2Fonts.getUTF8Width("Último update:"), buf, u8g2Fonts.getUTF8Width(buf), height - 50, 25);
}

void statusInfo()
// contents of the status bar.
{
  uint16_t width = display.width();
  uint16_t height = display.height();
  
  display.fillRect(0, height - 25 , width, 25, GxEPD_BLACK);
  display.updateWindow(0, height-25, width, 25, false);
  display.fillRect(0, height - 25 , width, 25, GxEPD_WHITE);
  display.updateWindow(0, height - 25, width, 25, false);

  u8g2Fonts.setFont(u8g2_font_open_iconic_embedded_2x_t);

  uint16_t x = 0;
  uint16_t y = height;
  u8g2Fonts.drawStr(x,y, "\x49");

  x = width / 2 - 32 ;
  y = height-2;

  u8g2Fonts.setFont(u8g2_font_helvB12_tf);
  u8g2Fonts.drawStr(x,y, "100%");
  display.updateWindow(0, height - 25, width, 25, false);
}

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong checkwifi_timeout    = 0;
  static ulong display_timeout = 0;
  static ulong current_millis = millis();

#define WIFICHECK_INTERVAL    1000L
#define DISPLAY_INTERVAL  30000L

#if USE_ESP_WIFIMANAGER_NTP
  #define HEARTBEAT_INTERVAL    60000L
#else
  #define HEARTBEAT_INTERVAL    10000L
#endif

#define LED_INTERVAL          2000L

  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    check_WiFi();
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }

  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0))
  {
    heartBeatPrint();
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
  }
  // Check display update every DISPLAY_INTERNAL (30) seconds.
  if ((current_millis > display_timeout) || (display_timeout == 0))
  {
    updateCredentials();
    additionalInfo();
    additionalInfo2();
    statusInfo();
    display_timeout = current_millis + DISPLAY_INTERVAL;
  }
}

int calcChecksum(uint8_t* address, uint16_t sizeToCalc)
{
  uint16_t checkSum = 0;
  
  for (uint16_t index = 0; index < sizeToCalc; index++)
  {
    checkSum += * ( ( (byte*) address ) + index);
  }

  return checkSum;
}

bool loadConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadWiFiCfgFile "));

  memset((void *) &WM_config,       0, sizeof(WM_config));

  // New in v1.4.0
  memset((void *) &WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));
  //////

  if (file)
  {
    file.readBytes((char *) &WM_config,   sizeof(WM_config));

    // New in v1.4.0
    file.readBytes((char *) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));

    if ( WM_config.checksum != calcChecksum( (uint8_t*) &WM_config, sizeof(WM_config) - sizeof(WM_config.checksum) ) )
    {
      LOGERROR(F("WM_config checksum wrong"));
      
      return false;
    }
    
    // New in v1.4.0
    displayIPConfigStruct(WM_STA_IPconfig);
    //////

    return true;
  }
  else
  {
    LOGERROR(F("failed"));

    return false;
  }
}

void saveConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveWiFiCfgFile "));

  if (file)
  {
    WM_config.checksum = calcChecksum( (uint8_t*) &WM_config, sizeof(WM_config) - sizeof(WM_config.checksum) );
    
    file.write((uint8_t*) &WM_config, sizeof(WM_config));

    displayIPConfigStruct(WM_STA_IPconfig);

    // New in v1.4.0
    file.write((uint8_t*) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));
  }
  else
  {
    LOGERROR(F("failed"));
  }
}

bool readConfigFile() 
{
  // this opens the config file in read-mode
  File f = FileFS.open(CONFIG_FILE, "r");

  if (!f)
  {
    Serial.println(F("Config File not found"));
    return false;
  }
  else
  {
    // we could open the file
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size + 1]);

    // Read and store file contents in buf
    f.readBytes(buf.get(), size);
    // Closing file
    f.close();
    // Using dynamic JSON buffer which is not the recommended memory model, but anyway
    // See https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model

    DynamicJsonDocument json(1024);
    auto deserializeError = deserializeJson(json, buf.get());
    
    if ( deserializeError )
    {
      Serial.println(F("JSON parseObject() failed"));
      return false;
    }
    
    serializeJson(json, Serial);
    
    // Parse all config file parameters, override
    // local config variables with parsed values
    if (json.containsKey(CREDENTIALS_JSON_Label))
    {
      strcpy(custom_CREDENTIALS_JSON, json[CREDENTIALS_JSON_Label]);
    }

    if (json.containsKey(INFO1_REST_Label))
    {
      strcpy(custom_INFO1_REST, json[INFO1_REST_Label]);
    }

    if (json.containsKey(INFO2_REST_Label))
    {
      strcpy(custom_INFO2_REST, json[INFO2_REST_Label]);
    }

  }
  
  Serial.println(F("\nConfig File successfully parsed"));
  
  return true;
}

bool writeConfigFile() 
{
  Serial.println(F("Saving Config File"));

  DynamicJsonDocument json(1024);
  // JSONify local configuration parameters
  json[CREDENTIALS_JSON_Label]  = custom_CREDENTIALS_JSON;
  json[INFO1_REST_Label]  = custom_INFO1_REST;
  json[INFO2_REST_Label]  = custom_INFO2_REST;
  // Open file for writing
  File f = FileFS.open(CONFIG_FILE, "w");

  if (!f)
  {
    Serial.println(F("Failed to open Config File for writing"));
    return false;
  }

  serializeJsonPretty(json, Serial);
  // Write data to file and close it
  serializeJson(json, f);

  f.close();

  Serial.println(F("\nConfig File successfully saved"));
  return true;
}

// this function is just to display newly saved data,
// it is not necessary though, because data is displayed
// after WiFi manager resets ESP32
void newConfigData() 
{
  Serial.println();
  Serial.print(F("custom_CREDENTIALS_JSON: ")); 
  Serial.println(custom_CREDENTIALS_JSON);
  Serial.println();
}


void wifi_manager() 
{
  Serial.println(F("\nConfig Portal requested."));
  //Local intialization. Once its business is done, there is no need to keep it around
  // Use this to default DHCP hostname to ESP8266-XXXXXX or ESP32-XXXXXX
  //ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer);
  // Use this to personalize DHCP hostname (RFC952 conformed)
  AsyncWebServer webServer(80);

#if ( USING_ESP32_S2 || USING_ESP32_C3 ) 
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, NULL, "QrCode DRD-FS INFO");
#else
  DNSServer dnsServer;
  
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "QrCode DRD-FS INFO");
#endif

  //Check if there is stored WiFi router/password credentials.
  //If not found, device will remain in configuration mode until switched off via webserver.
  Serial.print(F("Opening Configuration Portal. "));
  
  Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
  Router_Pass = ESPAsync_wifiManager.WiFi_Pass();
  
  // From v1.1.1, Don't permit NULL password
  if ( !initialConfig && (Router_SSID != "") && (Router_Pass != "") )
  {
    //If valid AP credential and not DRD, set timeout 120s.
    ESPAsync_wifiManager.setConfigPortalTimeout(120);
    Serial.println("Got stored Credentials. Timeout 120s");
  }
  else
  {
    ESPAsync_wifiManager.setConfigPortalTimeout(0);

    Serial.print(F("No timeout : "));
    
    if (initialConfig)
    {
      Serial.println(F("DRD or No stored Credentials.."));
    }
    else
    {
      Serial.println(F("No stored Credentials."));
    }
  }

  // Extra parameters to be configured
  // After connecting, parameter.getValue() will get you the configured value
  // Format: <ID> <Placeholder text> <default value> <length> <custom HTML> <label placement>
  // (*** we are not using <custom HTML> and <label placement> ***)


  ESPAsync_WMParameter CREDENTIALS_JSON_FIELD(CREDENTIALS_JSON_Label, "CREDENTIALS JSON", custom_CREDENTIALS_JSON, custom_CREDENTIALS_JSON_LEN + 1);
  ESPAsync_WMParameter INFO1_REST_FIELD(INFO1_REST_Label, "INFO1_REST", custom_INFO1_REST, custom_INFO1_REST_LEN + 1);
  ESPAsync_WMParameter INFO2_REST_FIELD(INFO2_REST_Label, "INFO2_REST", custom_INFO2_REST, custom_INFO2_REST_LEN + 1);

  // add all parameters here
  // order of adding is not important
  ESPAsync_wifiManager.addParameter(&CREDENTIALS_JSON_FIELD);
  ESPAsync_wifiManager.addParameter(&INFO1_REST_FIELD);
  ESPAsync_wifiManager.addParameter(&INFO2_REST_FIELD);

  // Sets timeout in seconds until configuration portal gets turned off.
  // If not specified device will remain in configuration mode until
  // switched off via webserver or device is restarted.
  //ESPAsync_wifiManager.setConfigPortalTimeout(120);

  ESPAsync_wifiManager.setMinimumSignalQuality(-1);

  // From v1.0.10 only
  // Set config portal channel, default = 1. Use 0 => random channel from 1-13
  ESPAsync_wifiManager.setConfigPortalChannel(0);
  //////

#if USE_CUSTOM_AP_IP 
  //set custom ip for portal
  // New in v1.4.0
  ESPAsync_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig);
  //////
#endif
  
#if !USE_DHCP_IP    
    // Set (static IP, Gateway, Subnetmask, DNS1 and DNS2) or (IP, Gateway, Subnetmask). New in v1.0.5
    // New in v1.4.0
    ESPAsync_wifiManager.setSTAStaticIPConfig(WM_STA_IPconfig);
    //////
#endif

  // New from v1.1.1
#if USING_CORS_FEATURE
  ESPAsync_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // Start an access point
  // and goes into a blocking loop awaiting configuration.
  // Once the user leaves the portal with the exit button
  // processing will continue
  // SSID to uppercase
  ssid.toUpperCase();
  password = "My" + ssid;

  Serial.print(F("Starting configuration portal @ "));
    
#if USE_CUSTOM_AP_IP    
  Serial.print(APStaticIP);
#else
  Serial.print(F("192.168.4.1"));
#endif

  Serial.print(F(", SSID = "));
  Serial.print(ssid);
  Serial.print(F(", PWD = "));
  Serial.println(password);

  if (!ESPAsync_wifiManager.startConfigPortal((const char *) ssid.c_str(), password.c_str()))
  {
    Serial.println(F("Not connected to WiFi but continuing anyway."));
  }
  else
  {
    // If you get here you have connected to the WiFi
    Serial.println(F("Connected...yeey :)"));
    Serial.print(F("Local IP: "));
    Serial.println(WiFi.localIP());
  }

  // Only clear then save data if CP entered and with new valid Credentials
  // No CP => stored getSSID() = ""
  if ( String(ESPAsync_wifiManager.getSSID(0)) != "" && String(ESPAsync_wifiManager.getSSID(1)) != "" )
  {
    // Stored  for later usage, from v1.1.0, but clear first
    memset(&WM_config, 0, sizeof(WM_config));
    
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      String tempSSID = ESPAsync_wifiManager.getSSID(i);
      String tempPW   = ESPAsync_wifiManager.getPW(i);
  
      if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);
  
      if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);  
  
      // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      }
    }

#if USE_ESP_WIFIMANAGER_NTP      
    String tempTZ   = ESPAsync_wifiManager.getTimezoneName();

    if (strlen(tempTZ.c_str()) < sizeof(WM_config.TZ_Name) - 1)
      strcpy(WM_config.TZ_Name, tempTZ.c_str());
    else
      strncpy(WM_config.TZ_Name, tempTZ.c_str(), sizeof(WM_config.TZ_Name) - 1);

    const char * TZ_Result = ESPAsync_wifiManager.getTZ(WM_config.TZ_Name);
    
    if (strlen(TZ_Result) < sizeof(WM_config.TZ) - 1)
      strcpy(WM_config.TZ, TZ_Result);
    else
      strncpy(WM_config.TZ, TZ_Result, sizeof(WM_config.TZ_Name) - 1);
         
    if ( strlen(WM_config.TZ_Name) > 0 )
    {
      LOGERROR3(F("Saving current TZ_Name ="), WM_config.TZ_Name, F(", TZ = "), WM_config.TZ);
      configTzTime(WM_config.TZ, "192.168.1.1", "0.pool.ntp.org", "1.pool.ntp.org");
    }
    else
    {
      LOGERROR(F("Current Timezone Name is not set. Enter Config Portal to set."));
    }
#endif

    // New in v1.4.0
    ESPAsync_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
    //////
    
    saveConfigData();
  }

  // Getting posted form values and overriding local variables parameters
  // Config file is written regardless the connection state
  strcpy(custom_CREDENTIALS_JSON, CREDENTIALS_JSON_FIELD.getValue());
  strcpy(custom_INFO1_REST, INFO1_REST_FIELD.getValue());
  strcpy(custom_INFO2_REST, INFO2_REST_FIELD.getValue());
   
  // Writing JSON config file to flash for next boot
  writeConfigFile();

}

void displaySetup()
{
    SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
    display.init(); // enable diagnostic output on Serial
    qrcode.init();
    u8g2Fonts.begin(display);
    u8g2Fonts.setFontMode(1);                           // use u8g2 transparent mode (this is default)
    u8g2Fonts.setFontDirection(0);                      // left to right (this is default)
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);          // apply Adafruit GFX color
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
}


void setup()
{
  // Put your setup code here, to run once
  Serial.begin(115200);
  while (!Serial);

  displaySetup();

  Serial.print(F("\nStarting Wifi QrCode Info using ")); Serial.print(FS_Name);
  Serial.print(F(" on ")); Serial.println(ARDUINO_BOARD);
  Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION);
  Serial.println(ESP_DOUBLE_RESET_DETECTOR_VERSION);

  if ( String(ESP_ASYNC_WIFIMANAGER_VERSION) < ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET )
  {
    Serial.print("Warning. Must use this example on Version later than : ");
    Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET);
  }

  Serial.setDebugOutput(false);

  // Mount the filesystem
  if (FORMAT_FILESYSTEM)
  {
    Serial.println(F("Forced Formatting."));
    FileFS.format();
  }

  // Format FileFS if not yet
  if (!FileFS.begin(true))
  {
    Serial.println(F("SPIFFS/LittleFS failed! Already tried formatting."));
  
    if (!FileFS.begin())
    {     
      // prevents debug info from the library to hide err message.
      delay(100);
      
      Serial.println(F("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever"));
      while (true)
      {
        delay(1);
      }
    }
  }

  // New in v1.4.0
  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);
  //////
  
  if (!readConfigFile())
  {
    Serial.println(F("Can't read Config File, using default values"));
  }

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

  if (!drd)
  {
    Serial.println(F("Can't instantiate. Disable DRD feature"));
  }
  else if (drd->detectDoubleReset())
  {
    // DRD, disable timeout.
    //ESPAsync_wifiManager.setConfigPortalTimeout(0);
    
    Serial.println(F("Open Config Portal without Timeout: Double Reset Detected"));
    initialConfig = true;
  }
 
  if (initialConfig)
  {
    wifi_manager();
  }
  else
  {   
    // Pretend CP is necessary as we have no AP Credentials
    initialConfig = true;

    // Load stored data, the addAP ready for MultiWiFi reconnection
    if (loadConfigData())
    {
#if USE_ESP_WIFIMANAGER_NTP      
    if ( strlen(WM_config.TZ_Name) > 0 )
    {
      LOGERROR3(F("Current TZ_Name ="), WM_config.TZ_Name, F(", TZ = "), WM_config.TZ);

  
      configTzTime(WM_config.TZ, "192.168.1.1", "0.pool.ntp.org", "1.pool.ntp.org");
    }
    else
    {
      Serial.println(F("Current Timezone is not set. Enter Config Portal to set."));
    } 
#endif
      
      for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
      {
        // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
        if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
        {
          LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
          wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
          initialConfig = false;
        }
      }
    }

    if (initialConfig)
    {
      Serial.println(F("Open Config Portal without Timeout: No stored WiFi Credentials"));
    
      wifi_manager();
    }
    else if ( WiFi.status() != WL_CONNECTED ) 
    {
      Serial.println("ConnectMultiWiFi in setup");
     
      connectMultiWiFi();
    }
  }
  
}


void loop()
{
  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  if (drd)
    drd->loop();
  // put your main code here, to run repeatedly
  check_status();
}