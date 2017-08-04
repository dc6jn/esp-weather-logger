// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h

#if defined(ARDUINO) && ARDUINO >= 100
    #include "Arduino.h"
#else
    #include "WProgram.h"
#endif

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "credentials.h"

extern float rawTempC;
extern float rawTempF;
extern float rawHumidity;
extern float filterTempC;
extern float filterTempF;
extern float filterHumidity;;

extern const long PinModeMask[6];
extern const byte PinModeShift[6];
extern const byte PinModePin[6];
//extern const string Topics[6];

extern uint16_t ulMeasCount ;  // values already measured
extern uint16_t ulNoMeasValues; // size of array
extern uint16_t currentIndex;

typedef struct
{
  time_t timestamp;
  int16_t temp;
  uint32_t pressure;
  uint16_t humid;
} MW;

extern MW *pMWbuf;

#define ModePin5   B00010000
#define ModePinA0  B00010000
#define MQTTEnable B00000001




float absFeuchte(float temp, float luftfeuchte, float pressure);

#define RELEASE  // Comment to enable debug output

#define DBG_OUTPUT_PORT Serial

#ifndef RELEASE
#define DEBUGLOG(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOG(...)
#endif

#define CONNECTION_LED -1 // Connection LED pin (Built in). -1 to disable
#define AP_ENABLE_BUTTON 16 // Button pin to enable AP during startup for configuration. -1 to disable


#define CONFIG_FILE "/config.json"
#define SECRET_FILE "/secret.json"


typedef struct {
    String APssid = "ESP"; // ChipID is appended to this name
    String APpassword = myAPPASSWD;
    bool APenable = false; // AP disabled by default
} strApConfig;

typedef struct {
    bool auth;
    String wwwUsername;
    String wwwPassword;
} strHTTPAuth;

typedef struct {
	String ssid;
	String password;
	IPAddress  ip;
	IPAddress  netmask;
	IPAddress  gateway;
	IPAddress  dns;
	bool dhcp;
	String ntpServerName;
	long updateNTPTimeEvery;
	long timezone;
	bool daylight;
	String deviceName;
	byte DeviceMode;
	long PinModes;
	String MQTTUser;
	String MQTTpassword;
	String MQTTHost;
	int MQTTPort;
	int RefreshInterval;
	String MQTTTopic;
	String ClientName;
	int PWMFreq;
	int pin3t;
	int pin4t;

} strConfig;



class AsyncFSWebServer : public AsyncWebServer {
public:
    AsyncFSWebServer(uint16_t port);
    void begin(FS* fs);
    void handle();
	String GetMacAddressLS();
	String zeroPad(int number);
	strConfig _config;
	int WiFiStatus();




protected:
    //strConfig _config; // General and WiFi configuration
    strApConfig _apConfig; // Static AP config settings
    strHTTPAuth _httpAuth;
    FS* _fs;
    long wifiDisconnectedSince = 0;
    String _browserMD5 = "";
    uint32_t _updateSize = 0;
  
    WiFiEventHandler onStationModeConnectedHandler, onStationModeDisconnectedHandler;

    //uint currentWifiStatus;

    Ticker _secondTk;
    bool _secondFlag;

    AsyncEventSource _evs = AsyncEventSource("/events");

    void sendTimeData();
    bool load_config();
    void defaultConfig();
    bool save_config();
    bool loadHTTPAuth();
    bool saveHTTPAuth();
    void configureWifiAP();
    void configureWifi();
    void ConfigureOTA(String password);
    void serverInit();

    void onWiFiConnected(WiFiEventStationModeConnected data);
    void onWiFiDisconnected(WiFiEventStationModeDisconnected data);

    static void s_secondTick(void* arg);

    String getMacAddress();

    bool checkAuth(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    //void handleFileRead_edit_html(AsyncWebServerRequest *request);
    bool handleFileRead(String path, AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void send_general_configuration_values_html(AsyncWebServerRequest *request);
    void send_network_configuration_values_html(AsyncWebServerRequest *request);
    void send_connection_state_values_html(AsyncWebServerRequest *request);
    void send_information_values_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void send_network_configuration_html(AsyncWebServerRequest *request);
    void send_general_configuration_html(AsyncWebServerRequest *request);
	void send_NTP_configuration_html(AsyncWebServerRequest *request);
	void send_MQTT_configuration_html(AsyncWebServerRequest *request);
    void restart_esp(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_html(AsyncWebServerRequest *request);
    void send_update_firmware_values_html(AsyncWebServerRequest *request);
    void setUpdateMD5(AsyncWebServerRequest *request);
    void updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
	void send_mqtt_configuration_values_html(AsyncWebServerRequest *request);
    static String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)
    static unsigned char h2int(char c);
    static boolean checkRange(String Value);
};

extern AsyncFSWebServer ESPHTTPServer;


#endif // _FSWEBSERVERLIB_h
