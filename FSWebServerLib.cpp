//
//
//


#include "FSWebServerLib.h"
#include <StreamString.h>
#include "SparkFunBME280.h"
#include "credentials.h"
AsyncFSWebServer ESPHTTPServer(80);

float absFeuchte(float temp, float luftfeuchte, float pressure) {
    float abs_feuchte = 0;
    float temp_k = temp + 273.15;
    float gas_konst = 8314.3;
    float mol_dampf = 18.016;
    float a;
    float b;
    float sdd_1;
    float sdd;
    float dd;
    float dd1;
    float abs_feuchte1;
    if (temp >= 0) {
        a = 7.5;
        b = 237.3;
    } else {
        // bei Temp unter Null und über Wasser
        a = 7.6;
        b = 240.7;
    }
    sdd_1 = ((a * temp) / (b + temp));
    sdd = 6.1078 * pow(10, sdd_1);
    dd = (luftfeuchte / 100) * sdd;
    dd1 = (dd / 6.1078);
    //    dew_point = log10(((luftfeuche/100)*sdd)/6.1078);
    //    v = log10(dd/6.1078);
    abs_feuchte1 = ((pressure * mol_dampf) / gas_konst) * (dd / temp_k);
    //abs_feuchte1 = (round(abs_feuchte1 * 10)) / 10;

    //https://carnotcycle.wordpress.com/2012/08/04/how-to-convert-relative-humidity-to-absolute-humidity/  
    //abs_feuchte1=6.112 * exp((17.67 * T)/(T+243.5)) * rh * 2.1674/(273.15+T)
    return abs_feuchte1;
}



const char Page_WaitAndReload[] PROGMEM = R"=====(
<meta http-equiv="refresh" content="10; URL=/config.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_Restart[] PROGMEM = R"=====(
<meta http-equiv="refresh" content="10; URL=/general.html">
Please Wait....Configuring and Restarting.
)=====";

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {
}

/*void AsyncFSWebServer::secondTick()
{
    _secondFlag = true;
}*/

/*void AsyncFSWebServer::secondTask() {
    //DEBUGLOG("%s\r\n", NTP.getTimeDateString().c_str());
    sendTimeData();
}*/

void AsyncFSWebServer::s_secondTick(void* arg) {
    AsyncFSWebServer* self = reinterpret_cast<AsyncFSWebServer*> (arg);
    if (self->_evs.count() > 0) {
        self->sendTimeData();
    }
}

void AsyncFSWebServer::sendTimeData() {
    String data = "{";
    data += "\"time\":\"" + NTP.getTimeStr() + "\",";
    data += "\"date\":\"" + NTP.getDateStr() + "\",";
    data += "\"lastSync\":\"" + NTP.getTimeDateString(NTP.getLastNTPSync()) + "\",";
    data += "\"uptime\":\"" + NTP.getUptimeString() + "\",";
    data += "\"lastBoot\":\"" + NTP.getTimeDateString(NTP.getLastBootTime()) + "\",";
    data += "}\r\n";
    DEBUGLOG(data.c_str());
    _evs.send(data.c_str(), "timeDate");
    DEBUGLOG("%s\r\n", NTP.getTimeDateString().c_str());
    data = String();
    //DEBUGLOG(__PRETTY_FUNCTION__);
    //DEBUGLOG("\r\n")
}

String formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + "B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0) + "KB";
    } else if (bytes < (1024 * 1024 * 1024)) {
        return String(bytes / 1024.0 / 1024.0) + "MB";
    } else {
        return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
    }
}

void flashLED(int pin, int times, int delayTime) {
    int oldState = digitalRead(pin);
    DEBUGLOG("---Flash LED during %d ms %d times. Old state = %d\r\n", delayTime, times, oldState);

    for (int i = 0; i < times; i++) {
        digitalWrite(pin, LOW); // Turn on LED
        delay(delayTime);
        digitalWrite(pin, HIGH); // Turn on LED
        delay(delayTime);
    }
    digitalWrite(pin, oldState); // Turn on LED
}

void AsyncFSWebServer::begin(FS* fs) {
    _fs = fs;
    Serial.begin(115200);
    Serial.print("\n\n");
#ifndef RELEASE
    Serial.setDebugOutput(true);
#endif // RELEASE
    // NTP client setup
    if (CONNECTION_LED >= 0) {
        pinMode(CONNECTION_LED, OUTPUT); // CONNECTION_LED pin defined as output
    }
    if (AP_ENABLE_BUTTON >= 0) {
        pinMode(AP_ENABLE_BUTTON, INPUT_PULLUP); // If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
    }
    //analogWriteFreq(200);

    if (AP_ENABLE_BUTTON >= 0) {
        _apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
        DEBUGLOG("AP Enable = %d\n", _apConfig.APenable);
    }

    if (CONNECTION_LED >= 0) {
        digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
    }

    if (!_fs) // If SPIFFS is not started
        _fs->begin();
#ifndef RELEASE
    { // List files
        Dir dir = _fs->openDir("/");
        while (dir.next()) {
            String fileName = dir.fileName();
            size_t fileSize = dir.fileSize();
            DEBUGLOG("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
        }
        DEBUGLOG("\n");
    }
#endif // RELEASE
    if (!load_config()) { // Try to load configuration from file system
        defaultConfig(); // Load defaults if any error
        _apConfig.APenable = true;
    }
    loadHTTPAuth();
    //WIFI INIT
    if (_config.updateNTPTimeEvery > 0) { // Enable NTP sync
        NTP.begin(_config.ntpServerName, _config.timezone / 10, _config.daylight);
        NTP.setInterval(15, _config.updateNTPTimeEvery * 60);

        NTP.onNTPSyncEvent([](NTPSyncEvent_t ntpEvent) {
            if (ntpEvent) {
                Serial.print("Time Sync error: ");
                if (ntpEvent == noResponse)
                    Serial.println("NTP server not reachable");
                else if (ntpEvent == invalidAddress)
                    Serial.println("Invalid NTP server address");
            } else {
                Serial.print("Got NTP time: ");
                Serial.println(NTP.getTimeDateString(NTP.getLastNTPSync()));
            }
        });
    }



    // Register wifi Event to control connection LED
    onStationModeConnectedHandler = WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) {
        this->onWiFiConnected(data);
    });

    onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) {
        this->onWiFiDisconnected(data);
    });

    onSoftAPModeStationConnectedhandler = WiFi.onSoftAPModeStationConnected([this](WiFiEventSoftAPModeStationConnected data) {
        this->onSoftAPModeStationConnected(data);
    });

    onSoftAPModeStationDisconnectedhandler = WiFi.onSoftAPModeStationDisconnected([this](WiFiEventSoftAPModeStationDisconnected data) {
        this->onSoftAPModeStationDisconnected(data);
    });

    WiFi.hostname(_config.deviceName.c_str());

    //if (!configureWifi()) { configureWifiAP();} // Set WiFi config
    configureWifi(false); // Set WiFi config

    DEBUGLOG("Open http://");
    DEBUGLOG(_config.deviceName.c_str());
    DEBUGLOG(".local/edit to see the file browser\r\n");
    DEBUGLOG("Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
    DEBUGLOG("Scketch size: %u\r\n", ESP.getSketchSize());
    DEBUGLOG("Free flash space: %u\r\n", ESP.getFreeSketchSpace());

    _secondTk.attach(1.0f, &AsyncFSWebServer::s_secondTick, static_cast<void*> (this)); // Task to run periodic things every second

    AsyncWebServer::begin();
    serverInit(); // Configure and start Web server


    //MDNS.begin(_config.deviceName.c_str()); // I've not got this to work. Need some investigation.
    //MDNS.addService("http", "tcp", 80);
    ConfigureOTA(_httpAuth.wwwPassword.c_str());
    DEBUGLOG("END Setup\n");
}

bool AsyncFSWebServer::load_config() {
    File configFile = _fs->open(CONFIG_FILE, "r");
    if (!configFile) {
        DEBUGLOG("Failed to open config file");
        return false;
    }

    size_t size = configFile.size();
    /*if (size > 1024) {
        DEBUGLOG("Config file size is too large");
        configFile.close();
        return false;
    }*/

    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[] > buf(new char[size]);

    // We don't use String here because ArduinoJson library requires the input
    // buffer to be mutable. If you don't use ArduinoJson, you may as well
    // use configFile.readString instead.
    configFile.readBytes(buf.get(), size);
    configFile.close();
    DEBUGLOG("JSON file size: %d bytes\r\n", size);
    DynamicJsonBuffer jsonBuffer(1024);
    //StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());

    if (!json.success()) {
        DEBUGLOG("Failed to parse config file\r\n");
        return false;
    }
#ifndef RELEASE
    String temp;
    json.prettyPrintTo(temp);
    Serial.println(temp);
#endif

    _config.ssid = json["ssid"].as<const char *>();

    _config.password = json["pass"].as<const char *>();

    _config.ip = IPAddress(json["ip"][0], json["ip"][1], json["ip"][2], json["ip"][3]);
    _config.netmask = IPAddress(json["netmask"][0], json["netmask"][1], json["netmask"][2], json["netmask"][3]);
    _config.gateway = IPAddress(json["gateway"][0], json["gateway"][1], json["gateway"][2], json["gateway"][3]);
    _config.dns = IPAddress(json["dns"][0], json["dns"][1], json["dns"][2], json["dns"][3]);

    _config.dhcp = json["dhcp"].as<bool>();

    _config.ntpServerName = json["ntp"].as<const char *>();
    _config.updateNTPTimeEvery = json["NTPperiod"].as<long>();
    _config.timezone = json["timeZone"].as<long>();
    _config.daylight = json["daylight"].as<long>();
    _config.deviceName = json["deviceName"].as<const char *>();

    //config.connectionLed = json["led"];
    _config.MQTTUser = json["MQTTUser"].as<const char *>();
    _config.MQTTpassword = json["MQTTpassword"].as<const char *>();
    _config.MQTTHost = json["MQTTHost"].as<const char *>();
    _config.deviceName = json["deviceName"].as<const char *>();
    _config.MQTTTopic = json["MQTTTopic"].as<const char *>();
    _config.ClientName = json["ClientName"].as<const char *>();
    _config.MQTTPort = json["MQTTPort"].as<int>();
    _config.MQTTRefreshInterval = json["RefreshInterval"].as<int>();
    _config.DeviceMode = json["DeviceMode"].as<byte>();
    _config.PinModes = json["PinModes"].as<long>();
    _config.PWMFreq = json["PWMFreq"].as<int>();
    _config.LogFreq = json["LogFreq"].as<int>();
    _config.pin3t = json["pin3t"].as<int>();
    _config.pin4t = json["pin4t"].as<int>();

    DEBUGLOG("Data initialized.\n");
    DEBUGLOG("SSID: %s ", _config.ssid.c_str());
    DEBUGLOG("PASS: %s\r\n", _config.password.c_str());
    DEBUGLOG("NTP Server: %s\r\n", _config.ntpServerName.c_str());
    //Serial.printf("Connection LED: %d\n", config.connectionLed);
    DEBUGLOG(__PRETTY_FUNCTION__);
    return true;
}

void AsyncFSWebServer::defaultConfig() {
    // DEFAULT CONFIG
    _config.ssid = mySSID;
    _config.password = myWIFIPASSWD;
    _config.dhcp = 1;
    _config.ip = IPAddress(192, 168, 1, 4);
    _config.netmask = IPAddress(255, 255, 255, 0);
    _config.gateway = IPAddress(192, 168, 1, 1);
    _config.dns = IPAddress(192, 168, 1, 1);
    _config.ntpServerName = "pool.ntp.org";
    _config.updateNTPTimeEvery = 15;
    _config.timezone = 10;
    _config.daylight = 1;
    _config.deviceName = "ESP8266fs";
    //config.connectionLed = CONNECTION_LED;
    _config.DeviceMode = 0;
    _config.PinModes = 0;
    _config.MQTTUser = "User";
    _config.MQTTpassword = "Pass";
    _config.MQTTHost = "mqtt.cloudemqtt.com";
    _config.MQTTPort = 1883;
    _config.ClientName = "UNSET";
    _config.MQTTRefreshInterval = 5;
    _config.PWMFreq = 1000;
    _config.LogFreq = 600;
    _config.pin3t = 0;
    _config.pin4t = 0;
    save_config();
    DEBUGLOG(__PRETTY_FUNCTION__);
    DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::save_config() {
    //flag_config = false;
    DEBUGLOG("Save config");
    DynamicJsonBuffer jsonBuffer(512);
    //StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["ssid"] = _config.ssid;
    json["pass"] = _config.password;

    JsonArray& jsonip = json.createNestedArray("ip");
    jsonip.add(_config.ip[0]);
    jsonip.add(_config.ip[1]);
    jsonip.add(_config.ip[2]);
    jsonip.add(_config.ip[3]);

    JsonArray& jsonNM = json.createNestedArray("netmask");
    jsonNM.add(_config.netmask[0]);
    jsonNM.add(_config.netmask[1]);
    jsonNM.add(_config.netmask[2]);
    jsonNM.add(_config.netmask[3]);

    JsonArray& jsonGateway = json.createNestedArray("gateway");
    jsonGateway.add(_config.gateway[0]);
    jsonGateway.add(_config.gateway[1]);
    jsonGateway.add(_config.gateway[2]);
    jsonGateway.add(_config.gateway[3]);

    JsonArray& jsondns = json.createNestedArray("dns");
    jsondns.add(_config.dns[0]);
    jsondns.add(_config.dns[1]);
    jsondns.add(_config.dns[2]);
    jsondns.add(_config.dns[3]);

    json["dhcp"] = _config.dhcp;
    json["ntp"] = _config.ntpServerName;
    json["NTPperiod"] = _config.updateNTPTimeEvery;
    json["timeZone"] = _config.timezone;
    json["daylight"] = _config.daylight;
    json["deviceName"] = _config.deviceName;

    json["DeviceMode"] = _config.DeviceMode;
    json["PinModes"] = _config.PinModes;
    json["MQTTUser"] = _config.MQTTUser;
    json["MQTTpassword"] = _config.MQTTpassword;
    json["MQTTHost"] = _config.MQTTHost;
    json["MQTTPort"] = _config.MQTTPort;
    json["RefreshInterval"] = _config.MQTTRefreshInterval;
    json["MQTTTopic"] = _config.MQTTTopic;
    json["ClientName"] = _config.ClientName;
    json["PWMFreq"] = _config.PWMFreq;
    json["LogFreq"] = _config.LogFreq;
    json["pin3t"] = _config.pin3t;
    json["pin4t"] = _config.pin4t;


    //json["led"] = config.connectionLed;

    //TODO add AP data to html
    File configFile = _fs->open(CONFIG_FILE, "w");
    if (!configFile) {
        DEBUGLOG("Failed to open config file for writing\r\n");
        configFile.close();
        return false;
    }

#ifndef RELEASE
    String temp;
    json.prettyPrintTo(temp);
    DEBUGLOG(temp.c_str());
#endif

    json.printTo(configFile);
    configFile.flush();
    configFile.close();
    return true;
}

bool AsyncFSWebServer::loadHTTPAuth() {
    File configFile = _fs->open(SECRET_FILE, "r");
    if (!configFile) {
        DEBUGLOG("Failed to open secret file\r\n");
        _apConfig.auth = false;
        _httpAuth.auth = false;
        _httpAuth.wwwUsername = "";
        _httpAuth.wwwPassword = "";
        configFile.close();
        return false;
    }

    size_t size = configFile.size();
    /*if (size > 256) {
        Serial.printf("Secret file size is too large\r\n");
        httpAuth.auth = false;
        configFile.close();
        return false;
    }*/

    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[] > buf(new char[size]);

    // We don't use String here because ArduinoJson library requires the input
    // buffer to be mutable. If you don't use ArduinoJson, you may as well
    // use configFile.readString instead.
    configFile.readBytes(buf.get(), size);
    configFile.close();
    DEBUGLOG("JSON secret file size: %d bytes\n", size);
    DynamicJsonBuffer jsonBuffer(256);
    //StaticJsonBuffer<256> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());

    if (!json.success()) {
#ifndef RELEASE
        String temp;
        json.prettyPrintTo(temp);
        DEBUGLOG(temp.c_str());
        DEBUGLOG("Failed to parse secret file\n");
#endif // RELEASE
        _httpAuth.auth = false;
        _apConfig.auth = false;
        return false;
    }
#ifndef RELEASE
    String temp;
    json.prettyPrintTo(temp);
    DEBUGLOG(temp.c_str());
#endif // RELEASE
    _apConfig.auth = json["apauth"];
    _httpAuth.auth = json["auth"];
    _httpAuth.wwwUsername = json["user"].as<String>();
    _httpAuth.wwwPassword = json["pass"].as<String>();
#ifndef RELEASE
    Serial.println(_httpAuth.auth ? F("Secret initialized.") : F("Auth disabled."));
    Serial.println(_apConfig.auth ? F("AP Secret initialized.") : F("AP Auth disabled."));
    if (_httpAuth.auth) {
        Serial.printf("User: %s\r\n", _httpAuth.wwwUsername.c_str());
        Serial.printf("Pass: %s\r\n", _httpAuth.wwwPassword.c_str());
    }
    if (_apConfig.auth) {

        Serial.printf("AP Pass: %s\r\n", _httpAuth.wwwPassword.c_str());
    }
    Serial.printf(__PRETTY_FUNCTION__);
    Serial.printf("\r\n");
#endif
    return true;
}

void AsyncFSWebServer::handle() {
    ArduinoOTA.handle();
}

bool AsyncFSWebServer::configureWifiAP() {
    Serial.printf(__PRETTY_FUNCTION__);
    Serial.printf("\r\n");
    //WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    String APname = _apConfig.APssid + (String) ESP.getChipId();
    if (_apConfig.auth) {
        WiFi.softAP(APname.c_str(), _httpAuth.wwwPassword.c_str(), 11);
        Serial.printf("AP Pass enabled: %s\r\n", _httpAuth.wwwPassword.c_str());

    } else {
        WiFi.softAP(APname.c_str());
        Serial.println(F("AP Pass disabled"));
    }
    if (CONNECTION_LED >= 0) {
        flashLED(CONNECTION_LED, 3, 250);
    }
    return true;
}

bool AsyncFSWebServer::configureWifi(bool save_permanent) {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    Serial.printf("Connecting to %s\r\n", _config.ssid.c_str());
    if (WiFi.status() != WL_CONNECTED) { // FIX FOR USING 2.3.0 CORE (only .begin if not connected)
        WiFi.persistent(save_permanent); //-> http://www.forum-raspberrypi.de/Thread-esp8266-achtung-flash-speicher-schreibzugriff-bei-jedem-aufruf-von-u-a-wifi-begin
        WiFi.begin(_config.ssid.c_str(), _config.password.c_str(), 2);
        if (!_config.dhcp) {
            Serial.println(F("NO DHCP"));
            WiFi.config(_config.ip, _config.gateway, _config.netmask, _config.dns);
        }
    }

    byte timeout = 20;
    while (!WiFi.isConnected()) {
        delay(1000);
        timeout--;
        if (timeout == 0)
            break; //leave loop
        Serial.print(".");
    }
    //Start AccessPoint:
    String APname = _apConfig.APssid + (String) ESP.getChipId();
    if (_apConfig.auth) {
        WiFi.softAP(APname.c_str(), _httpAuth.wwwPassword.c_str(), WiFi.channel(0));
        Serial.printf("AP Pass enabled: %s\r\n", _httpAuth.wwwPassword.c_str());

    } else {
        WiFi.softAP(APname.c_str());
        Serial.println(F("AP Pass disabled"));
    }

    WiFi.printDiag(Serial);
    Serial.println();
    Serial.print(F("APIP Address: "));
    Serial.println(WiFi.softAPIP());

    Serial.print(F("IP Address: "));
    Serial.println(WiFi.localIP());

    Serial.print(F("Gateway:    "));
    Serial.println(WiFi.gatewayIP());
    Serial.print(F("DNS:        "));
    Serial.println(WiFi.dnsIP());
    Serial.println(__PRETTY_FUNCTION__);
    return true;
}

void AsyncFSWebServer::ConfigureOTA(String password) {
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(_config.deviceName.c_str());

    // No authentication by default
    if (password != "") {
        ArduinoOTA.setPassword(password.c_str());
        Serial.print(F("OTA password set "));
        Serial.println(password);
    }

    //#ifndef RELEASE
    ArduinoOTA.onStart([]() {
        Serial.println("StartOTA");
    });
    ArduinoOTA.onEnd(std::bind([](FS * fs) {
        fs->end();
        Serial.println("End OTA");
    }, _fs));
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r\n", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
        });
    Serial.println(F("OTA Ready"));
    //#endif // RELEASE
    ArduinoOTA.begin();
}

void AsyncFSWebServer::onWiFiConnected(WiFiEventStationModeConnected data) {
    if (CONNECTION_LED >= 0) {
        digitalWrite(CONNECTION_LED, LOW); // Turn LED on
    }
    Serial.printf("Led %d on\n", CONNECTION_LED);
    //turnLedOn();
    wifiDisconnectedSince = 0;
}

void AsyncFSWebServer::onSoftAPModeStationConnected(WiFiEventSoftAPModeStationConnected data) {
    Serial.printf("Stations connected to soft-AP = %d\n", WiFi.softAPgetStationNum());
    //WiFi.enableSTA(false);    Serial.println("disable STA");
}

void AsyncFSWebServer::onSoftAPModeStationDisconnected(WiFiEventSoftAPModeStationDisconnected data) {
    Serial.printf("Stations connected to soft-AP = %d\n", WiFi.softAPgetStationNum());
    Serial.println("Station disconnected");
    if (WiFi.softAPgetStationNum() < 1) WiFi.mode(WIFI_AP_STA);
};

void AsyncFSWebServer::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
    Serial.println(F("case STA_DISCONNECTED"));

    if (CONNECTION_LED >= 0) {
        digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
    }
    //Serial.printf("Led %s off\n", CONNECTION_LED);
    //flashLED(config.connectionLed, 2, 100);
    if (wifiDisconnectedSince == 0) {
        wifiDisconnectedSince = millis();
    }
    Serial.printf("Disconnected for %ds\r\n", (int) ((millis() - wifiDisconnectedSince) / 1000));
    if ((int) ((millis() - wifiDisconnectedSince) / 1000) > 30) {
        Serial.println("stop scanning");
        WiFi.mode(WIFI_AP); // WiFi.enableSTA (false);
    }

}

void AsyncFSWebServer::handleFileList(AsyncWebServerRequest *request) {
    if (!request->hasArg("dir")) {
        request->send(500, "text/plain", "BAD ARGS");
        return;
    }

    String path = request->arg("dir");
    Serial.printf("handleFileList: %s\r\n", path.c_str());
    Dir dir = _fs->openDir(path);
    path = String();

    String output = "[";
    while (dir.next()) {
        File entry = dir.openFile("r");
        if (true)//entry.name()!="secret.json") // Do not show secrets
        {
            if (output != "[")
                output += ',';
            bool isDir = false;
            output += "{\"type\":\"";
            output += (isDir) ? "dir" : "file";
            output += "\",\"name\":\"";
            output += String(entry.name()).substring(1);
            output += "\"}";
        }
        entry.close();
    }

    output += "]";
    Serial.printf("%s\r\n", output.c_str());
    request->send(200, "text/json", output);
}

String getContentType(String filename, AsyncWebServerRequest *request) {
    if (request->hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".json")) return "application/json";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

bool AsyncFSWebServer::handleFileRead(String path, AsyncWebServerRequest *request) {
    //Serial.printf("handleFileRead: %s\r\n", path.c_str());
    if (CONNECTION_LED >= 0) {
        // CANNOT RUN DELAY() INSIDE CALLBACK
        flashLED(CONNECTION_LED, 1, 25); // Show activity on LED
    }
    if (path.endsWith("/"))
        path += "index.html";
    String contentType = getContentType(path, request);
    String pathWithGz = path + ".gz";
    if (_fs->exists(pathWithGz) || _fs->exists(path)) {
        if (_fs->exists(pathWithGz)) {
            path += ".gz";
        }
        //Serial.print(F("Content type: "));
        //Serial.println(contentType);
        AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
        if (path.endsWith(".gz"))
            response->addHeader("Content-Encoding", "gzip");
        //File file = SPIFFS.open(path, "r");
        //Serial.printf("File %s exist\r\n", path.c_str());
        request->send(response);
        //Serial.print("file sent");
        return true;
    } else
        Serial.print(F("Cannot find "));
    //Serial.print(path);
    return false;
}

void AsyncFSWebServer::handleFileCreate(AsyncWebServerRequest *request) {
    if (!checkAuth(request))
        return request->requestAuthentication();
    if (request->args() == 0)
        return request->send(500, "text/plain", "BAD ARGS");
    String path = request->arg(0U);
    Serial.printf("handleFileCreate: %s\r\n", path.c_str());
    if (path == "/")
        return request->send(500, "text/plain", "BAD PATH");
    if (_fs->exists(path))
        return request->send(500, "text/plain", "FILE EXISTS");
    File file = _fs->open(path, "w");
    if (file)
        file.close();
    else
        return request->send(500, "text/plain", "CREATE FAILED");
    request->send(200, "text/plain", "");
    path = String(); // Remove? Useless statement?
}

void AsyncFSWebServer::handleFileDelete(AsyncWebServerRequest *request) {
    if (!checkAuth(request))
        return request->requestAuthentication();
    if (request->args() == 0) return request->send(500, "text/plain", "BAD ARGS");
    String path = request->arg(0U);
    Serial.printf("handleFileDelete: %s\r\n", path.c_str());
    if (path == "/")
        return request->send(500, "text/plain", "BAD PATH");
    if (!_fs->exists(path))
        return request->send(404, "text/plain", "FileNotFound");
    _fs->remove(path);
    request->send(200, "text/plain", "");
    path = String(); // Remove? Useless statement?
}

void AsyncFSWebServer::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File fsUploadFile;
    static size_t fileSize = 0;

    if (!index) { // Start
        Serial.printf("handleFileUpload Name: %s\r\n", filename.c_str());
        if (!filename.startsWith("/")) filename = "/" + filename;
        fsUploadFile = _fs->open(filename, "w");
        Serial.printf("First upload part.\r\n");

    }
    // Continue
    if (fsUploadFile) {
        Serial.printf("Continue upload part. Size = %u\r\n", len);
        if (fsUploadFile.write(data, len) != len) {
            Serial.println("Write error during upload");
        } else
            fileSize += len;
    }
    /*for (size_t i = 0; i < len; i++) {
        if (fsUploadFile)
            fsUploadFile.write(data[i]);
    }*/
    if (final) { // End
        if (fsUploadFile) {
            fsUploadFile.close();
        }
        Serial.printf("handleFileUpload Size: %u\n", fileSize);
        fileSize = 0;
    }
}

void AsyncFSWebServer::send_general_configuration_values_html(AsyncWebServerRequest *request) {
    String values = "";
    values += "devicename|" + (String) _config.deviceName + "|input\n";
    values += "in1|" + (String) ((_config.PinModes & PinModeMask[0]) >> PinModeShift[0]) + "|input\n";
    values += "in2|" + (String) ((_config.PinModes & PinModeMask[1]) >> PinModeShift[1]) + "|input\n";
    values += "out1|" + (String) ((_config.PinModes & PinModeMask[2]) >> PinModeShift[2]) + "|input\n";
    values += "out2|" + (String) ((_config.PinModes & PinModeMask[3]) >> PinModeShift[3]) + "|input\n";
    values += "pin1|" + (String) ((_config.PinModes & PinModeMask[4]) >> PinModeShift[4]) + "|input\n";
    values += "pin2|" + (String) ((_config.PinModes & PinModeMask[5]) >> PinModeShift[5]) + "|input\n";
    values += "pwmf|" + (String) _config.PWMFreq + "|input\n";
    values += "logf|" + (String) _config.LogFreq + "|input\n";
    values += "pin3t|" + (String) _config.pin3t + "|input\n";
    values += "pin4t|" + (String) _config.pin4t + "|input\n";


    values += "en|" + (String) (_config.DeviceMode & ModePinA0 ? "checked" : "") + "|chk\n";
    request->send(200, "text/plain", values);
    DEBUGLOG(__FUNCTION__);
    DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_network_configuration_values_html(AsyncWebServerRequest *request) {

    String values = "";

    values += "ssid|" + (String) _config.ssid + "|input\n";
    //    if(_httpAuth.auth)
    //      {values += "password|" + (String) _config.password + "|input\n";}
    values += "ip_0|" + (String) _config.ip[0] + "|input\n";
    values += "ip_1|" + (String) _config.ip[1] + "|input\n";
    values += "ip_2|" + (String) _config.ip[2] + "|input\n";
    values += "ip_3|" + (String) _config.ip[3] + "|input\n";
    values += "nm_0|" + (String) _config.netmask[0] + "|input\n";
    values += "nm_1|" + (String) _config.netmask[1] + "|input\n";
    values += "nm_2|" + (String) _config.netmask[2] + "|input\n";
    values += "nm_3|" + (String) _config.netmask[3] + "|input\n";
    values += "gw_0|" + (String) _config.gateway[0] + "|input\n";
    values += "gw_1|" + (String) _config.gateway[1] + "|input\n";
    values += "gw_2|" + (String) _config.gateway[2] + "|input\n";
    values += "gw_3|" + (String) _config.gateway[3] + "|input\n";
    values += "dns_0|" + (String) _config.dns[0] + "|input\n";
    values += "dns_1|" + (String) _config.dns[1] + "|input\n";
    values += "dns_2|" + (String) _config.dns[2] + "|input\n";
    values += "dns_3|" + (String) _config.dns[3] + "|input\n";
    values += "dhcp|" + (String) (_config.dhcp ? "checked" : "") + "|chk\n";

    request->send(200, "text/plain", values);
    values = "";

    Serial.printf(__PRETTY_FUNCTION__);
    Serial.printf("\r\n");
}

void AsyncFSWebServer::send_connection_state_values_html(AsyncWebServerRequest *request) {

    String state = "N/A";
    String Networks = "";
    if (WiFi.status() == 0) state = "Idle";
    else if (WiFi.status() == 1) state = "NO SSID AVAILBLE";
    else if (WiFi.status() == 2) state = "SCAN COMPLETED";
    else if (WiFi.status() == 3) state = "CONNECTED";
    else if (WiFi.status() == 4) state = "CONNECT FAILED";
    else if (WiFi.status() == 5) state = "CONNECTION LOST";
    else if (WiFi.status() == 6) state = "DISCONNECTED";

    WiFi.scanNetworks(true);

    String values = "";
    values += "connectionstate|" + state + "|div\n";
    //values += "networks|Scanning networks ...|div\n";
    request->send(200, "text/plain", values);
    state = "";
    values = "";
    Networks = "";
    Serial.printf(__FUNCTION__);
    Serial.printf("\r\n");
}

void AsyncFSWebServer::send_information_values_html(AsyncWebServerRequest *request) {

    String values = "";

    values += "x_ssid|" + (String) WiFi.SSID() + "|div\n";
    values += "x_ip|" + (String) WiFi.localIP()[0] + "." + (String) WiFi.localIP()[1] + "." + (String) WiFi.localIP()[2] + "." + (String) WiFi.localIP()[3] + "|div\n";
    values += "x_gateway|" + (String) WiFi.gatewayIP()[0] + "." + (String) WiFi.gatewayIP()[1] + "." + (String) WiFi.gatewayIP()[2] + "." + (String) WiFi.gatewayIP()[3] + "|div\n";
    values += "x_netmask|" + (String) WiFi.subnetMask()[0] + "." + (String) WiFi.subnetMask()[1] + "." + (String) WiFi.subnetMask()[2] + "." + (String) WiFi.subnetMask()[3] + "|div\n";
    values += "x_mac|" + getMacAddress() + "|div\n";
    values += "x_dns|" + (String) WiFi.dnsIP()[0] + "." + (String) WiFi.dnsIP()[1] + "." + (String) WiFi.dnsIP()[2] + "." + (String) WiFi.dnsIP()[3] + "|div\n";
    values += "x_ntp_sync|" + NTP.getTimeDateString(NTP.getLastNTPSync()) + "|div\n";
    values += "x_ntp_time|" + NTP.getTimeStr() + "|div\n";
    values += "x_ntp_date|" + NTP.getDateStr() + "|div\n";
    values += "x_uptime|" + NTP.getUptimeString() + "|div\n";
    values += "x_last_boot|" + NTP.getTimeDateString(NTP.getLastBootTime()) + "|div\n";

    request->send(200, "text/plain", values);
    //delete &values;
    values = "";
    Serial.printf(__FUNCTION__);
    Serial.printf("\r\n");

}

String AsyncFSWebServer::getMacAddress() {
    uint8_t mac[6];
    char macStr[18] = {0};
    WiFi.macAddress(mac);
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

void AsyncFSWebServer::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {

    String values = "";
    values += "ntpserver|" + (String) _config.ntpServerName + "|input\n";
    values += "update|" + (String) _config.updateNTPTimeEvery + "|input\n";
    values += "tz|" + (String) _config.timezone + "|input\n";
    values += "dst|" + (String) (_config.daylight ? "checked" : "") + "|chk\n";
    request->send(200, "text/plain", values);
    DEBUGLOG(__FUNCTION__);
    DEBUGLOG("\r\n");

}

// convert a single hex digit character to its integer value (from https://code.google.com/p/avr-netino/)

unsigned char AsyncFSWebServer::h2int(char c) {
    if (c >= '0' && c <= '9') {
        return ((unsigned char) c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return ((unsigned char) c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return ((unsigned char) c - 'A' + 10);
    }
    return (0);
}

String AsyncFSWebServer::urldecode(String input) // (based on https://code.google.com/p/avr-netino/)
{
    char c;
    String ret = "";

    for (byte t = 0; t < input.length(); t++) {
        c = input[t];
        if (c == '+') c = ' ';
        if (c == '%') {


            t++;
            c = input[t];
            t++;
            c = (h2int(c) << 4) | h2int(input[t]);
        }

        ret.concat(c);
    }
    return ret;

}

void AsyncFSWebServer::send_mqtt_configuration_values_html(AsyncWebServerRequest *request) {

    String values = "";
    values += "Host|" + (String) _config.MQTTHost + "|input\n";
    values += "Port|" + (String) _config.MQTTPort + "|input\n";
    values += "User|" + (String) _config.MQTTUser + "|input\n";
    //    if (_httpAuth.auth){
    //     values += "Pass|" + (String) _config.MQTTpassword + "|input\n";
    //    }
    values += "en|" + (String) (_config.DeviceMode & MQTTEnable ? "checked" : "") + "|chk\n";
    values += "upd|" + (String) _config.MQTTRefreshInterval + "|input\n";
    values += "topic|" + (String) _config.MQTTTopic + "|input\n";
    values += "ftopic|/" + (String) _config.MQTTTopic + "/" + _config.ClientName + "/|div\n";

    request->send(200, "text/plain", values);
    DEBUGLOG(__FUNCTION__);
    DEBUGLOG("\r\n");
}

//
// Check the Values is between 0-255
//

boolean AsyncFSWebServer::checkRange(String Value) {
    if (Value.toInt() < 0 || Value.toInt() > 255) {
        return false;
    } else {
        return true;
    }
}

void AsyncFSWebServer::send_network_configuration_html(AsyncWebServerRequest *request) {
    Serial.printf(__FUNCTION__);
    Serial.printf("\r\n");


    if (request->args() > 0) // Save Settings
    {
        //String temp = "";
        bool oldDHCP = _config.dhcp; // Save status to avoid general.html cleares it
        _config.dhcp = false;
        for (uint8_t i = 0; i < request->args(); i++) {
            Serial.printf("Arg %d: %s\r\n", i, request->arg(i).c_str());
            if (request->argName(i) == "devicename") {
                _config.deviceName = urldecode(request->arg(i));
                _config.dhcp = oldDHCP;
                continue;
            }
            if (request->argName(i) == "ssid") {
                _config.ssid = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "password") {
                _config.password = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "ip_0") {
                if (checkRange(request->arg(i))) _config.ip[0] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "ip_1") {
                if (checkRange(request->arg(i))) _config.ip[1] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "ip_2") {
                if (checkRange(request->arg(i))) _config.ip[2] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "ip_3") {
                if (checkRange(request->arg(i))) _config.ip[3] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "nm_0") {
                if (checkRange(request->arg(i))) _config.netmask[0] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "nm_1") {
                if (checkRange(request->arg(i))) _config.netmask[1] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "nm_2") {
                if (checkRange(request->arg(i))) _config.netmask[2] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "nm_3") {
                if (checkRange(request->arg(i))) _config.netmask[3] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "gw_0") {
                if (checkRange(request->arg(i))) _config.gateway[0] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "gw_1") {
                if (checkRange(request->arg(i))) _config.gateway[1] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "gw_2") {
                if (checkRange(request->arg(i))) _config.gateway[2] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "gw_3") {
                if (checkRange(request->arg(i))) _config.gateway[3] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "dns_0") {
                if (checkRange(request->arg(i))) _config.dns[0] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "dns_1") {
                if (checkRange(request->arg(i))) _config.dns[1] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "dns_2") {
                if (checkRange(request->arg(i))) _config.dns[2] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "dns_3") {
                if (checkRange(request->arg(i))) _config.dns[3] = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "dhcp") {
                _config.dhcp = true;
                continue;
            }
        }
        request->send_P(200, "text/html", Page_WaitAndReload);
        save_config();
        configureWifi(true); //save credentials to flash
        //yield();
        delay(2000);
        _fs->end();
        ESP.restart();
        //AdminTimeOutCounter = 0;
    } else {
        DEBUGLOG(request->url().c_str());
        handleFileRead(request->url(), request);
    }
    Serial.printf(__PRETTY_FUNCTION__);
    Serial.printf("\r\n");
}

void AsyncFSWebServer::send_general_configuration_html(AsyncWebServerRequest *request) {
    Serial.printf(__FUNCTION__);
    Serial.printf("\r\n");

    if (!checkAuth(request))
        return request->requestAuthentication();

    if (request->args() > 0) // Save Settings
    {
        for (int i = 0; i < sizeof (PinModeMask); i++) {
            _config.PinModes = _config.PinModes & ~PinModeMask[i];
        }
        Serial.println(String(_config.DeviceMode));
        _config.DeviceMode = _config.DeviceMode & ~ModePinA0;
        Serial.println(String(_config.DeviceMode));

        for (uint8_t i = 0; i < request->args(); i++) {
            Serial.printf("Arg %d: %s\r\n", i, request->arg(i).c_str());
            if (request->argName(i) == "devicename") {
                _config.deviceName = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "in1") {
                _config.PinModes = _config.PinModes | ((long) request->arg(i).toInt() << PinModeShift[0]);
                continue;
            }
            if (request->argName(i) == "in2") {
                _config.PinModes = _config.PinModes | ((long) request->arg(i).toInt() << PinModeShift[1]);
                continue;
            }
            if (request->argName(i) == "out1") {
                _config.PinModes = _config.PinModes | ((long) request->arg(i).toInt() << PinModeShift[2]);
                continue;
            }
            if (request->argName(i) == "out2") {
                _config.PinModes = _config.PinModes | ((long) request->arg(i).toInt() << PinModeShift[3]);
                continue;
            }
            if (request->argName(i) == "pin1") {
                _config.PinModes = _config.PinModes | ((long) request->arg(i).toInt() << PinModeShift[4]);
                continue;
            }
            if (request->argName(i) == "pin2") {
                _config.PinModes = _config.PinModes | ((long) request->arg(i).toInt() << PinModeShift[5]);
                continue;
            }

            if (request->argName(i) == "en") {
                _config.DeviceMode = _config.DeviceMode | ModePinA0;
                continue;
            }
            if (request->argName(i) == "pwmf") {
                _config.PWMFreq = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "logf") {
                _config.LogFreq = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "pin3t") {
                _config.pin3t = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "pin4t") {
                _config.pin4t = request->arg(i).toInt();
                continue;
            }
        }
        Serial.println(String(_config.DeviceMode));

        request->send_P(200, "text/html", Page_Restart);
        save_config();
        _fs->end();
        ESP.restart();
        //ConfigureWifi();
        //AdminTimeOutCounter = 0;
    } else {
        handleFileRead(request->url(), request);
    }

    Serial.println(__PRETTY_FUNCTION__);
}

void AsyncFSWebServer::send_NTP_configuration_html(AsyncWebServerRequest *request) {

    if (!checkAuth(request))
        return request->requestAuthentication();

    if (request->args() > 0) // Save Settings
    {
        _config.daylight = false;
        //String temp = "";
        for (uint8_t i = 0; i < request->args(); i++) {
            if (request->argName(i) == "ntpserver") {
                _config.ntpServerName = urldecode(request->arg(i));
                NTP.setNtpServerName(_config.ntpServerName);
                continue;
            }
            if (request->argName(i) == "update") {
                _config.updateNTPTimeEvery = request->arg(i).toInt();
                NTP.setInterval(_config.updateNTPTimeEvery * 60);
                continue;
            }
            if (request->argName(i) == "tz") {
                _config.timezone = request->arg(i).toInt();
                NTP.setTimeZone(_config.timezone / 10);
                continue;
            }
            if (request->argName(i) == "dst") {
                _config.daylight = true;
                Serial.printf("Daylight Saving: %d\r\n", _config.daylight);
                continue;
            }
        }

        NTP.setDayLight(_config.daylight);
        save_config();
        //firstStart = true;

        setTime(NTP.getTime()); //set time
    }
    handleFileRead("/ntp.html", request);
    //server.send(200, "text/html", PAGE_NTPConfiguration);
    Serial.printf(__PRETTY_FUNCTION__);
    Serial.printf("\r\n");

}

void AsyncFSWebServer::send_MQTT_configuration_html(AsyncWebServerRequest *request) {

    if (!checkAuth(request))
        return request->requestAuthentication();

    if (request->args() > 0) // Save Settings
    {

        //String temp = "";
        for (uint8_t i = 0; i < request->args(); i++) {
            if (request->argName(i) == "Host") {
                _config.MQTTHost = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "Port") {
                _config.MQTTPort = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "User") {
                _config.MQTTUser = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "Pass") {
                _config.MQTTpassword = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "en") {
                _config.DeviceMode = _config.DeviceMode | MQTTEnable;
                continue;
            }
            if (request->argName(i) == "upd") {
                _config.MQTTRefreshInterval = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "topic") {
                _config.MQTTTopic = urldecode(request->arg(i));
                continue;
            }


        }
        _config.ClientName = _config.deviceName + "_" + GetMacAddressLS();
        save_config();
        //firstStart = true;

    }
    handleFileRead("/mqtt.html", request);
    //server.send(200, "text/html", PAGE_NTPConfiguration);
    Serial.printf(__PRETTY_FUNCTION__);
    Serial.printf("\r\n");

}

void AsyncFSWebServer::restart_esp(AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", Page_Restart);
    Serial.printf(__FUNCTION__);
    Serial.printf("\r\n");
    _fs->end(); // SPIFFS.end();
    delay(1000);
    ESP.restart();
}

void AsyncFSWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
    String values = "";
    values += "apauth|" + (String) (_apConfig.auth ? "checked" : "") + "|chk\n";
    values += "wwwauth|" + (String) (_httpAuth.auth ? "checked" : "") + "|chk\n";
    values += "wwwuser|" + (String) _httpAuth.wwwUsername + "|input\n";
    values += "wwwpass|" + (String) _httpAuth.wwwPassword + "|input\n";

    request->send(200, "text/plain", values);

    Serial.printf(__FUNCTION__);
    Serial.printf("\r\n");
}

void AsyncFSWebServer::send_wwwauth_configuration_html(AsyncWebServerRequest *request) {
    Serial.printf("%s %d\n", __FUNCTION__, request->args());
    if (request->args() > 0) // Save Settings
    {
        _httpAuth.auth = false;
        //String temp = "";
        for (uint8_t i = 0; i < request->args(); i++) {
            if (request->argName(i) == "wwwuser") {
                _httpAuth.wwwUsername = urldecode(request->arg(i));
                Serial.printf("User: %s\n", _httpAuth.wwwUsername.c_str());
                continue;
            }
            if (request->argName(i) == "wwwpass") {
                _httpAuth.wwwPassword = urldecode(request->arg(i));
                Serial.printf("Pass: %s\n", _httpAuth.wwwPassword.c_str());
                continue;
            }
            if (request->argName(i) == "wwwauth") {
                _httpAuth.auth = true;
                Serial.printf("HTTP Auth enabled\r\n");
                continue;
            }
            if (request->argName(i) == "apauth") {
                _apConfig.auth = true;
                Serial.printf("AP Auth enabled\r\n");
                continue;
            }
        }

        saveHTTPAuth();
    }
    handleFileRead("/system.html", request);

    //Serial.printf(__PRETTY_FUNCTION__);
    //Serial.printf("\r\n");
}

bool AsyncFSWebServer::saveHTTPAuth() {
    //flag_config = false;
    Serial.println("Save secret");
    DynamicJsonBuffer jsonBuffer(256);
    //StaticJsonBuffer<256> jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["apauth"] = _apConfig.auth;
    json["auth"] = _httpAuth.auth;
    json["user"] = _httpAuth.wwwUsername;
    json["pass"] = _httpAuth.wwwPassword;

    //TODO add AP data to html
    File configFile = _fs->open(SECRET_FILE, "w");
    if (!configFile) {
        Serial.print("Failed to open secret file for writing");
        configFile.close();
        return false;
    }

#ifndef RELEASE
    String temp;
    json.prettyPrintTo(temp);
    Serial.println(temp);
#endif // RELEASE

    json.printTo(configFile);
    configFile.flush();
    configFile.close();
    return true;
}

void AsyncFSWebServer::send_update_firmware_values_html(AsyncWebServerRequest *request) {
    String values = "";
    uint32_t maxSketchSpace = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
    //bool updateOK = Update.begin(maxSketchSpace);
    bool updateOK = maxSketchSpace < ESP.getFreeSketchSpace();
    StreamString result;
    Update.printError(result);
    Serial.printf("--MaxSketchSpace: %d\n", maxSketchSpace);
    Serial.printf("--Update error = %s\n", result.c_str());
    values += "remupd|" + (String) ((updateOK) ? "OK" : "ERROR") + "|div\n";

    if (Update.hasError()) {
        result.trim();
        values += "remupdResult|" + result + "|div\n";
    } else {
        values += "remupdResult||div\n";
    }

    request->send(200, "text/plain", values);
    DEBUGLOG(__FUNCTION__);
    DEBUGLOG("\r\n");
}

void AsyncFSWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
    _browserMD5 = "";
    Serial.printf("Arg number: %d\r\n", request->args());
    if (request->args() > 0) // Read hash
    {
        for (uint8_t i = 0; i < request->args(); i++) {
            Serial.printf("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
            if (request->argName(i) == "md5") {
                _browserMD5 = urldecode(request->arg(i));
                Update.setMD5(_browserMD5.c_str());
                continue;
            }
            if (request->argName(i) == "size") {
                _updateSize = request->arg(i).toInt();
                Serial.printf("Update size: %l\r\n", _updateSize);
                continue;
            }
        }
        request->send(200, "text/html", "OK --> MD5: " + _browserMD5);
    }

}

void AsyncFSWebServer::settime(AsyncWebServerRequest *request) {
    _browserTS = 0;
    Serial.printf("Arg number: %d\r\n", request->args());
    if (request->args() > 0) // Read timestamp
    {
        for (uint8_t i = 0; i < request->args(); i++) {
            Serial.printf("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
            if (request->argName(i) == "ts") {
                _browserTS = request->arg(i).toInt();
                Serial.println(_browserTS);
                time_t old = now();
                setTime(_browserTS);
                time_t off = now() - old;
                Serial.print(F("old Time:"));
                Serial.print(old);
                Serial.println(NTP.getTimeDateString(old));
                Serial.print(F("new Time:"));
                Serial.print(now());
                Serial.println(NTP.getTimeDateString(now()));

                adjust_timestamps(off);
                continue;
            }
            //            if (request->argName(i) == "size") {
            //                _updateSize = request->arg(i).toInt();
            //                Serial.printf("Update size: %l\r\n", _updateSize);
            //                continue;
            //            }
        }
        request->send(200, "text/html", "OK --> TS: " + String(_browserTS));
    }

}

void AsyncFSWebServer::updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // handler for the file upload, get's the sketch bytes, and writes
    // them through the Update object
    static long totalSize = 0;
    if (!index) { //UPLOAD_FILE_START
        SPIFFS.end();
        Update.runAsync(true);
        Serial.printf("Update start: %s\n", filename.c_str());
        uint32_t maxSketchSpace = ESP.getSketchSize();
        Serial.printf("Max free scketch space: %u\n", maxSketchSpace);
        Serial.printf("New scketch size: %u\n", _updateSize);
        if (_browserMD5 != NULL && _browserMD5 != "") {
            Update.setMD5(_browserMD5.c_str());
            Serial.printf("Hash from client: %s\n", _browserMD5.c_str());
        }
        if (!Update.begin(_updateSize)) {//start with max available size
            Update.printError(Serial);
        }

    }

    // Get upload file, continue if not start
    totalSize += len;
    Serial.print(".");
    size_t written = Update.write(data, len);
    if (written != len) {
        Serial.printf("len = %d, written = %l, totalSize = %l\r\n", len, written, totalSize);
        //Update.printError(Serial);
        //return;
    }
    if (final) { // UPLOAD_FILE_END
        String updateHash;
        Serial.println("Applying update...");
        if (Update.end(true)) { //true to set the size to the current progress
            updateHash = Update.md5String();
            Serial.printf("Upload finished. Calculated MD5: %s\n", updateHash.c_str());
            Serial.printf("Update Success: %u\nRebooting...\n", request->contentLength());
        } else {
            updateHash = Update.md5String();
            Serial.printf("Upload failed. Calculated MD5: %s\n", updateHash.c_str());
            Update.printError(Serial);
        }
    }

    //delay(2);
}

void AsyncFSWebServer::serverInit() {
    //SERVER INIT
    //list directory
    on("/list", HTTP_GET, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->handleFileList(request);
    });
    //load editor
    on("/edit", HTTP_GET, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        if (!this->handleFileRead("/edit.html", request))
            request->send(404, "text/plain", "FileNotFound");
    });
    //create file
    on("/edit", HTTP_PUT, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->handleFileCreate(request);
    }); //delete file
    on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->handleFileDelete(request);
    });
    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    on("/edit", HTTP_POST, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", ""); }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            this->handleFileUpload(request, filename, index, data, len, final);
        });
    on("/admin/generalvalues", HTTP_GET, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_general_configuration_values_html(request);
    });
    on("/admin/values", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_network_configuration_values_html(request);
    });
    on("/admin/connectionstate", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_connection_state_values_html(request);
    });
    on("/admin/infovalues", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_information_values_html(request);
    });
    on("/admin/ntpvalues", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_NTP_configuration_values_html(request);
    });
    on("/config.html", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_network_configuration_html(request);
    });
    on("/scan", HTTP_GET, [](AsyncWebServerRequest * request) {
        String json = "[";
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);
        } else if (n) {
            for (int i = 0; i < n; ++i) {
                if (i) json += ",";
                        json += "{";
                        json += "\"rssi\":" + String(WiFi.RSSI(i));
                        json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
                        json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
                        json += ",\"channel\":" + String(WiFi.channel(i));
                        json += ",\"secure\":" + String(WiFi.encryptionType(i));
                        json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
                        json += "}";
                }
            WiFi.scanDelete();
            if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
                WiFi.scanNetworks(true);
            }
        }
        json += "]";
        request->send(200, "text/json", json);
        json = "";
    });
    on("/general.html", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_general_configuration_html(request);
    });
    on("/ntp.html", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_NTP_configuration_html(request);
    });
    on("/mqtt.html", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_MQTT_configuration_html(request);

    });
    on("/admin/restart", [this](AsyncWebServerRequest * request) {
        Serial.println(request->url());
        if (!this->checkAuth(request))
            return request->requestAuthentication();
                this->restart_esp(request);
        });
    on("/admin/wwwauth", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_wwwauth_configuration_values_html(request);
    });
    on("/admin/mqttvalues", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_mqtt_configuration_values_html(request);
    });

    on("/admin", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        if (!this->handleFileRead("/admin.html", request))
            request->send(404, "text/plain", "FileNotFound");
    });
    on("/charts", [this](AsyncWebServerRequest * request) {
        if (!this->handleFileRead("/charts.html", request))
            request->send(404, "text/plain", "FileNotFound");
    });
    on("/system.html", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_wwwauth_configuration_html(request);
    });
    on("/update/updatepossible", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->send_update_firmware_values_html(request);
    });
    on("/setmd5", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        //Serial.println("md5?");
        this->setUpdateMD5(request);
    });
    on("/settime", [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        this->settime(request);
    });
    on("/update", HTTP_GET, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        if (!this->handleFileRead("/update.html", request))
            request->send(404, "text/plain", "FileNotFound");
    });
    on("/update", HTTP_POST, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
        this->_fs->end();
        ESP.restart();
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        this->updateFirmware(request, filename, index, data, len, final);
    });


    //called when the url is not defined here
    //use it to load content from SPIFFS
    onNotFound([this](AsyncWebServerRequest * request) {
        Serial.printf("Not found: %s\r\n", request->url().c_str());
        if (!this->checkAuth(request))
            return request->requestAuthentication();
                AsyncWebServerResponse * response = request->beginResponse(200);
                response->addHeader("Connection", "close");
                response->addHeader("Access-Control-Allow-Origin", "*");
            if (!this->handleFileRead(request->url(), request))
                    request->send(404, "text/plain", "FileNotFound");
                    delete response; // Free up memory!
            });

    _evs.onConnect([](AsyncEventSourceClient * client) {
        Serial.printf("Event source client connected from %s\r\n", client->client()->remoteIP().toString().c_str());
    });
    addHandler(&_evs);

#define HIDE_SECRET
#ifdef HIDE_SECRET
    on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });
#endif // HIDE_SECRET

    //#define HIDE_CONFIG
#ifdef HIDE_CONFIG
    on(CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest * request) {
        if (!this->checkAuth(request))
            return request->requestAuthentication();
        AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });
#endif // HIDE_CONFIG

    //get heap status, analog input value and all GPIO statuses in one json call
    on("/all", HTTP_GET, [](AsyncWebServerRequest * request) {
        String json = "{";
        json += "\"ts\":\"" + String(now()) + "\",";
        json += "\"time\":\"" + NTP.getTimeStr() + "\",";
        json += "\"date\":\"" + NTP.getDateStr() + "\",";
        json += "\"heap\":" + String(ESP.getFreeHeap());
        json += ", \"analog\":" + String(analogRead(A0));
        json += ", \"gpio\":" + String((uint32_t) (((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
        extern BME280 bme;
        float temp(NAN), relhum(NAN), abshum(NAN), pres(NAN);
        temp = bme.readTempC();
        pres = bme.readFloatPressure();
        relhum = bme.readFloatHumidity();
        abshum = absFeuchte(temp, relhum, pres);

        abshum = absFeuchte(temp, relhum, pres);
        json += ",\"pressure\":" + String(pres / 100);
        json += ",\"temp\":" + String(temp);
        json += ",\"relhumid\":" + String(relhum);
        json += ",\"abshumid\":" + String(abshum);
        json += "}";
        request->send(200, "text/json", json);
        json = String();
    });

    //get heap status, analog input value and all GPIO statuses in one json call
    on("/datarange", HTTP_GET, [](AsyncWebServerRequest * request) {
        String json = "{";
        json += "\"time\":\"" + NTP.getTimeStr() + "\",";
        json += "\"date\":\"" + NTP.getDateStr() + "\",";
        json += "\"heap\":" + String(ESP.getFreeHeap());
        json += ", \"analog\":" + String(analogRead(A0));
        json += ", \"data_cnt\":" + String(ulNoMeasValues);
        json += "}";
        request->send(200, "text/json", json);
        json = String();
    });


    on("/dataval", HTTP_GET, [](AsyncWebServerRequest * request) {
        Serial.printf("Arg number: %d\r\n", request->args());
        uint16_t idx = 0;
        if (request->args() > 0) // Read hash
        {
            for (uint8_t i = 0; i < request->args(); i++) {
                Serial.printf("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
                if (request->argName(i) == "n") {
                    idx = request->arg(i).toInt();
                    continue;
                }
            }
            if ((idx >= 0)&&(idx < ulNoMeasValues)) {
                String json = "{";
                        json += "\"time\":\"" + NTP.getTimeStr() + "\",";
                        json += "\"date\":\"" + NTP.getDateStr() + "\",";
                        json += "\"heap\":" + String(ESP.getFreeHeap());

                        json += ",\"timestamp\":" + String(pMWbuf[idx].timestamp);
                        json += ",\"pressure\":" + String(pMWbuf[idx].pressure);
                        json += ",\"temp\":" + String(pMWbuf[idx].temp);
                        json += ",\"abshumid\":" + String(pMWbuf[idx].humid);

                        request->send(200, "text/json", json);
            } else {
                request->send(404, "text/plain", "invalid range");
            }
        }
    });

    //=====================================


    on("/dat", HTTP_GET, [this](AsyncWebServerRequest * request) {
        Serial.print(F("dat request\n"));
        AsyncWebServerResponse *response = request->beginResponse(
                String("text/plain"),
                sizeof (MW) * ulNoMeasValues,
                [](uint8_t *buffer, size_t maxLen, size_t alreadySent) -> size_t {
                    if ((sizeof (MW) * ulNoMeasValues + alreadySent) > maxLen) {
                        // We have more to read than fits in maxLen Buffer
                        memcpy((char*) buffer, (char*) pMWbuf + alreadySent, maxLen);
                        return maxLen;
                        //Serial.printf("sent %d from %d Bytes\n",alreadySent,maxLen); 
                    }
                    // Ok, last chunk
                    memcpy((char*) buffer, (char*) pMWbuf + alreadySent, sizeof (MW) * ulNoMeasValues - alreadySent);
                    return (sizeof (MW) * ulNoMeasValues - alreadySent); // Return from here to end of indexhtml
                }
        );
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Server", "ESP");
        request->send(response);
        Serial.println("dat request exit");
    });
    //---------------
    //    on("/datc", HTTP_GET, [this](AsyncWebServerRequest * request) {
    //
    //        AsyncWebServerResponse *response = request->beginChunkedResponse(
    //                "text/plain", [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
    //
    //                    size_t arrLen = sizeof (MW) * ulNoMeasValues;
    //                    size_t leftToWrite = arrLen - index;
    //                    if (!leftToWrite)
    //                        return 0; //end of transfer
    //                        size_t willWrite = (leftToWrite > maxLen) ? maxLen : leftToWrite;
    //                            memcpy(buffer, (char*) buffer + index, willWrite);
    //                            index += willWrite;
    //                        return willWrite;
    //                    });
    //
    //        response->addHeader("Server", "MyServerString");
    //        request->send(response);
    //    });
    //    //---------------


    //°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°



    //server.begin(); --> Not here
    Serial.println(F("HTTP server started"));
}

bool AsyncFSWebServer::checkAuth(AsyncWebServerRequest *request) {
    if (!_httpAuth.auth) {
        return true;
    } else {
        return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
    }

}

void AsyncFSWebServer::adjust_timestamps(time_t offset) {
    Serial.printf("Offset: %d\n", offset);
    if (offset < 10) return;
    for (int i = 0; i < ulNoMeasValues; i++) {
        if (pMWbuf[i].timestamp < 1500000000) {
            if (pMWbuf[i].timestamp > 0) {
                pMWbuf[i].timestamp = pMWbuf[i].timestamp + offset;
            }
        }
        //Serial.printf(" i=%d time=%d\n", i, pMWbuf[i].timestamp);
    }
}

String AsyncFSWebServer::GetMacAddressLS() {
    uint8_t mac[6];
    char macStr[18] = {0};
    WiFi.macAddress(mac);
    sprintf(macStr, "%02X%02X", mac[4], mac[5]);
    return String(macStr);
}

//String AsyncFSWebServer::zeroPad(int number) // pad out left of int with ZERO for units less than 10 ,
//{
//	if (number<10)
//	{
//		return "0" + String(number);
//	}
//	else
//	{
//		return "" + String(number);
//	}
//}

int AsyncFSWebServer::WiFiStatus() {
    return WiFi.status();
}

