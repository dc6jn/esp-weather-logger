/*
  Name:		ESP8266-weather-logger.ino
  Created:	
  Author:	dc6jn
  Credits:      this sketch is heavily based on MQTT_ESP8266.ino made by Lee
		to see the original work follow https://github.com/species5618/MQTT_ESP8266

  Warning:	I removed a lot of code from Lee to simplify things a bit and to have more space for logging data
*/

/* ==== Includes BME280==== */
#include "SparkFunBME280.h"
#include "Wire.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

#include <ESP8266WiFi.h>
#include "FS.h"
#include <WiFiClient.h>
#include <Time.h>
#include <NtpClientLib.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "FSWebServerLib.h"
#include <Hash.h>
#include "global.h"

#include "credentials.h"
AsyncWebSocket ws("/ws");


// #define DISABLEOUT
//#define RELEASE  // Comment to enable debug output
#define DBG_OUTPUT_PORT Serial

#ifndef RELEASE
#define DEBUGLOG(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOG(...)
#endif


long lastinputstate[6] = { 0, 0, 0, 0, 0, 0 };
// long lastinputstatet[6] = { 0,0,0,0,0,0 };
int lastA0 = -1;
//bool coldstart = true;

bool TimeSync = false;

WiFiClient espClient;
PubSubClient client(espClient);


//Freq Managment, lasy coding style for speed
void rpm0()     //This is the function that the interupt calls
{
  freqCounter[0]++;  //This function measures the rising and falling edge of the hall effect sensors signal frequency
  totaliser[0]++;  //counter
}

void Fcount()
{
  cli();            //Disable interrupts
  freq[0] = freqCounter[0];
  freqCounter[0] = 0;
  sei();            //Enables interrupts
}


FunctionPointer rpmList[1] = { rpm0};//, rpm1, rpm2, rpm3, rpm4, rpm5 };


void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    DEBUGLOG((char)payload[i]);
    DEBUGLOG("\r\n");
    message += (char)payload[i];
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    DEBUGLOG(F("Attempting MQTT connection..."));
    // Attempt to connect
    if (client.connect(ESPHTTPServer._config.ClientName.c_str(), ESPHTTPServer._config.MQTTUser.c_str(), ESPHTTPServer._config.MQTTpassword.c_str(),
                       ("/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/LWT").c_str(), 1, true, "DOWN")) {
      DEBUGLOG("connected");
      // Once connected, publish an announcement...
      //client.publish("outTopic", "hello world");
      //// ... and resubscribe
      //client.subscribe("inTopic");
      client.unsubscribe("#");
      client.publish(("/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/LWT").c_str(), "UP", 1);
    }
    else {
      DEBUGLOG("failed, rc=");
      DEBUGLOG(client.state());
      DEBUGLOG(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }



  for (size_t i = 0; i < (sizeof(PinModeMask) / 4); i++)
  {
    //		Serial.println(String(i));
    if (
      (((ESPHTTPServer._config.PinModes & PinModeMask[i]) >> PinModeShift[i]) == pOutput) //subscribe to MQTT for output pins
      ||
      (((ESPHTTPServer._config.PinModes & PinModeMask[i]) >> PinModeShift[i]) == pPWMOut) //subscribe to MQTT for PWM output pins
    )  //is output pin
    {
      pinMode(PinModePin[i], OUTPUT);
      if (ESPHTTPServer._config.DeviceMode & MQTTEnable == MQTTEnable)
      {
        //subscribe to MQTT
        String topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + Topics[i];
        client.subscribe(topic.c_str(), 1);
      }
    }
    else
    {
      pinMode(PinModePin[i], INPUT);
    }

    if (((ESPHTTPServer._config.PinModes & PinModeMask[i]) >> PinModeShift[i]) == pFreqIn) // jut simple input
    {
      //subscribe to MQTT for totlaiser reset
      String topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + Topics[i] + "_TR";
      client.subscribe(topic.c_str(), 1);

    }
  }



}

void setup() {


  Serial.begin(115200); Serial.println();
  Serial.print( F("Compiled: "));
  Serial.print( F(__DATE__));
  Serial.print( F(", "));
  Serial.print( F(__TIME__));
  Serial.print( F(", "));
  Serial.println( F(__VERSION__));

#define getflashsize
#ifdef getflashsize
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
  Serial.printf("Flash real size: %u\n\n", realSize);

  Serial.printf("Flash ide  size: %u\n", ideSize);
  Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
  Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize) {
    Serial.println("Flash Chip configuration wrong!\n");
  } else {
    Serial.println("Flash Chip configuration ok.\n");
  }
  //---------------------------------------------------------------
#endif



  //specify I2C address.  Can be 0x77(default) or 0x76
  bme.settings.commInterface = I2C_MODE;
  bme.settings.I2CAddress = 0x77;
  //***Operation settings*****************************//

  //renMode can be:
  //  0, Sleep mode
  //  1 or 2, Forced mode
  //  3, Normal mode
  bme.settings.runMode = 3; //Normal mode

  //tStandby can be:
  //  0, 0.5ms
  //  1, 62.5ms
  //  2, 125ms
  //  3, 250ms
  //  4, 500ms
  //  5, 1000ms
  //  6, 10ms
  //  7, 20ms
  bme.settings.tStandby = 3;

  //filter can be off or number of FIR coefficients to use:
  //  0, filter off
  //  1, coefficients = 2
  //  2, coefficients = 4
  //  3, coefficients = 8
  //  4, coefficients = 16
  bme.settings.filter = 4;

  //tempOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.tempOverSample = 2;

  //pressOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.pressOverSample = 5;

  //humidOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.humidOverSample = 3;

  //Calling .begin() causes the settings to be loaded
  delay(10);  //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.
  //Serial.println(bme.begin(), HEX);
  //
  delay(20);
  while (bme.begin() != 0x60) {
    Serial.println("Could not find BME280 sensor!");
    delay(1000);
  }
  bme.reset();
  delay(20);
  while (bme.begin() != 0x60) {
    Serial.println("Could not find BME280 sensor!");
    delay(1000);
  }
  Serial.print("nach bme");

  // WiFi is started inside library
  SPIFFS.begin(); // Not really needed, checked inside library and started if needed
  ESPHTTPServer.begin(&SPIFFS);
  /* add setup code here */
 ws.onEvent(onWsEvent);
  ESPHTTPServer.addHandler(&ws);

  if (ESPHTTPServer._config.DeviceMode & MQTTEnable == MQTTEnable)
  {
    client.setServer(ESPHTTPServer._config.MQTTHost.c_str(), ESPHTTPServer._config.MQTTPort);
    client.setCallback(callback);
  }
 


  pinMode(A0, INPUT);
  //reconnect();
  // allocate ram for data storage
  uint32_t freem = system_get_free_heap_size() - KEEP_MEM_FREE;

  Serial.printf("free mem=%ul, record size=%d\n", freem, sizeof(MW));
  ulNoMeasValues = 200;//freem/sizeof(MW); //1440; // 1 Messwert je Minute -> 24h
  currentIndex = 0;
  pMWbuf = new MW[ulNoMeasValues];

  if (pMWbuf == NULL ) {
    ulNoMeasValues = 0;
    Serial.println("Error in memory allocation!");
  }
  else {
    Serial.printf("Allocated storage for %d data points.\n", ulNoMeasValues);
  }


  Serial.print("ende setup");

}

void loop() {
  /* add main program code here */
  if (second() != lastsecond2)
  { // do refresh
    lastsecond2 = second();
    if (lastsecond2 % 5 == 0)
    {
      DEBUGLOG(NTP.getUptimeString());
      float temp(NAN), relhum(NAN), abshum(NAN), pres(NAN);

      char buffer[10];
      temp = bme.readTempC();
      pres = bme.readFloatPressure();
      relhum = bme.readFloatHumidity();
      abshum = absFeuchte(temp, relhum, pres);
      Serial.print("e Temp: "); Serial.print(temp);
      Serial.print(" Hum: "); Serial.print(relhum);
      Serial.print(" absHum: "); Serial.print(abshum);
      Serial.print(" Druck: "); Serial.println(pres / 100);
      currentIndex = (currentIndex + 1) % ulNoMeasValues;
      if (TimeSync) {
        pMWbuf[currentIndex].timestamp = NTP.getTime();
      }
      else
      {
        pMWbuf[currentIndex].timestamp = now();

      }
      pMWbuf[currentIndex].temp = temp * 100;
      pMWbuf[currentIndex].pressure = pres;
      pMWbuf[currentIndex].humid = abshum * 100;
    }
  }

  if (!TimeSync && NTP.getLastNTPSync() > 0)
  {
    TimeSync = true;
    if (ESPHTTPServer._config.DeviceMode & MQTTEnable == MQTTEnable)
    {
      client.publish(("/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/Boot").c_str(),
                     (NTP.getTimeStr() + " " + NTP.getDateStr()).c_str());
    }

  }

  if (ESPHTTPServer.WiFiStatus() == 3)
  {
    if (!client.connected()) {
      Serial.println("try to connect mqtt Client");
      reconnect();
      Serial.println("mqtt Connect Client");


    }
    client.loop();

    if (second() != lastsecond)
    { // do refresh
      //catch F-in and copy counter to values, shoudl be 1000ms interupd, but not simple on esp286
      //Fcount();

      lastsecond = second();
      if (second() % 2 == 0)
      {
        if (ESPHTTPServer._config.DeviceMode & MQTTEnable == MQTTEnable)
        {
          //mqttcounter = 0;
          //coldstart = false;
          //check value for MQTT publish

          if ((ESPHTTPServer._config.DeviceMode & ModePinA0) == ModePinA0)
          {
            int A0 = analogRead(A0);
            if (lastA0 != A0 && updateCounter[APINUPDATECOUNTER] == 0)
            {
              updateCounter[APINUPDATECOUNTER] = ESPHTTPServer._config.RefreshInterval;
              lastA0 = A0;
              String topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + ApinTopic;
              client.publish(topic.c_str(), String(A0).c_str(), 1);
            }
          }

          float temp(NAN), relhum(NAN), abshum(NAN), pres(NAN);

          char buffer[10];
          temp = bme.readTempC();
          pres = bme.readFloatPressure();
          relhum = bme.readFloatHumidity();
          abshum = absFeuchte(temp, relhum, pres);
          String topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + "TempC";
          client.publish(topic.c_str(), String(temp).c_str(), 1);

          topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + "RelHumidity";
          client.publish(topic.c_str(), String(relhum).c_str(), 1);
          topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + "AbsHumidity";
          client.publish(topic.c_str(), String(abshum).c_str(), 1);

          topic = "/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/" + "Pressure";
          client.publish(topic.c_str(), String(pres).c_str(), 1);
        }
      }
    }

  }
  else
  {
    if (client.connected())
    {
      Serial.println("Disconnect Client");
      Serial.println(espClient.connected());
      client.disconnect();
    }
  }

  if (minute() != lasttime)
  {
    lasttime = minute();
    for (int i = 0; i < ulNoMeasValues; i++) {
      int idx = (i + currentIndex) % ulNoMeasValues;
      //Serial.print(idx);
       float temp(NAN), relhum(NAN), abshum(NAN), pres(NAN);
       temp=pMWbuf[idx].temp;
       pres=pMWbuf[idx].pressure;
       abshum=pMWbuf[idx].humid;
      // Serial.print(NTP.getTimeDateString(pMWbuf[idx].timestamp));
      Serial.printf("i=%d idx=%d time=%ul.", i, idx, pMWbuf[idx].timestamp);
      Serial.print("m Temp: "); Serial.print(temp/100);
      //Serial.print(" Hum: "); Serial.print(relhum);
      Serial.print(" absHum: "); Serial.print(abshum/100);
      Serial.print(" Druck: "); Serial.println(pres / 100);
    }
  }
}

float GetTempC()
{
  //return dht.readTemperature();
}

float GetHum()
{
  //return dht.readHumidity();
}


boolean IsNumeric(String str) {
  unsigned int stringLength = str.length();

  if (stringLength == 0) {
    return false;
  }

  boolean seenDecimal = false;

  for (unsigned int i = 0; i < stringLength; ++i) {
    if (isDigit(str.charAt(i))) {
      continue;
    }

    if (str.charAt(i) == '.') {
      if (seenDecimal) {
        return false;
      }
      seenDecimal = true;
      continue;
    }
    return false;
  }
  return true;
}


void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if (type == WS_EVT_CONNECT) {
   
  for (int i = 0; i < ulNoMeasValues; i++) {
      int idx = (i + currentIndex) % ulNoMeasValues;
      //Serial.print(idx);
       float temp(NAN), relhum(NAN), abshum(NAN), pres(NAN);
       temp=pMWbuf[idx].temp;
       pres=pMWbuf[idx].pressure;
       abshum=pMWbuf[idx].humid;
      // Serial.print(NTP.getTimeDateString(pMWbuf[idx].timestamp));
      Serial.printf("ws i=%d idx=%d time=%ul.", i, idx, pMWbuf[idx].timestamp);
      Serial.print("m Temp: "); Serial.print(temp/100);
       client->printf("%d %d %d",idx,pMWbuf[idx].timestamp ,pMWbuf[idx].temp);
    }
   // server->binary(client->id(), pMWbuf, ulNoMeasValues*sizeof(MW));
     
  }
}
