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

long lastinputstate[6] = { 0, 0, 0, 0, 0, 0 };
// long lastinputstatet[6] = { 0,0,0,0,0,0 };
int lastA0 = -1;
//bool coldstart = true;
float temp(NAN), relhum(NAN), abshum(NAN), pres(NAN);

bool TimeSync = false;

WiFiClient espClient;
PubSubClient MQTTclient(espClient);
String mainTopic = String("ESP_") + String(ESP.getChipId(), HEX);

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
    message += (char)payload[i];
  }
  Serial.print(message);
}

bool reconnect() {

  if (ESPHTTPServer._config.MQTTRefreshInterval > 0) {
    Serial.print(F("Attempting MQTT connection..."));
    // Attempt to connect
    int res;
    if (ESPHTTPServer._config.MQTTUser.length() == 0) {

      res = MQTTclient.connect(ESPHTTPServer._config.ClientName.c_str(),
                               ("/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/LWT").c_str(), 1, true, "DOWN");
    }

    else {
      res = MQTTclient.connect(ESPHTTPServer._config.ClientName.c_str(), ESPHTTPServer._config.MQTTUser.c_str(), ESPHTTPServer._config.MQTTpassword.c_str(),
                               ("/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/LWT").c_str(), 1, true, "DOWN");
    }
    if (res) {
      Serial.print("connected\n");
      // Once connected, publish an announcement...
      //client.publish("outTopic", "hello world");
      //// ... and resubscribe
      //client.subscribe("inTopic");
      MQTTclient.unsubscribe("#");
      MQTTclient.publish(("/" + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName + "/LWT").c_str(), "UP", 1);
      return true;
    }
    else {
      Serial.print(F("failed, rc="));
      Serial.print(MQTTclient.state());
      Serial.print(F(" try again in 5 seconds\n"));
      // Wait 5 seconds before retrying
      return false;
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

  //#define getflashsize
#ifdef getflashsize
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Serial.printf("F real id:   %08X\n", ESP.getFlashChipId());
  Serial.printf("F real size: %u\n\n", realSize);

  Serial.printf("F ide  size: %u\n", ideSize);
  Serial.printf("F speed: %u\n", ESP.getFlashChipSpeed());
  Serial.printf("F ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
  Serial.print(F("Flash Chip config "));
  Serial.println(ideSize != realSize ? F("wrong!") : F("ok."));

  //---------------------------------------------------------------
#endif



  //specify I2C address.  Can be 0x77(default) or 0x76
  bme.settings.commInterface = I2C_MODE;
  
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
  delay(30);
  bme.settings.I2CAddress = 0x77;
  uint8_t timeout=10;
  while (bme.begin() != 0x60) {
    Serial.println(F("Could not find BME280 sensor @0x77!"));
    delay(1000);
    timeout-=1;
    if (timeout==0){
      timeout=10;
      bme.settings.I2CAddress = 0x76;
      while (bme.begin() != 0x60) {
        delay(1000);
        timeout--;
        if (timeout==0){
          Serial.println(F("Could not find BME280 sensor @0x76!"));
          break;
        }
    }
  }
  }
  bme.reset();
  delay(20);
  while (bme.begin() != 0x60) {
    Serial.println(F("Problems with BME280!"));
    delay(1000);
  }


  // WiFi is started inside library
  SPIFFS.begin(); // Not really needed, checked inside library and started if needed
  ESPHTTPServer.begin(&SPIFFS);
  /* add setup code here */
  //ws.onEvent(onWsEvent);
  //ESPHTTPServer.addHandler(&ws);

  if (ESPHTTPServer._config.MQTTRefreshInterval > 0)
  {
    MQTTclient.setServer(ESPHTTPServer._config.MQTTHost.c_str(), ESPHTTPServer._config.MQTTPort);
    MQTTclient.setCallback(callback);
  }

  pinMode(A0, INPUT);

  // allocate ram for data storage
  uint32_t freem = system_get_free_heap_size() - KEEP_MEM_FREE;

  Serial.printf("free mem=%ul, record size=%d\n", freem, sizeof(MW));
  ulNoMeasValues = 200;//freem/sizeof(MW); //1440; // 1 Messwert je Minute -> 24h
  currentIndex = 0;
  pMWbuf = new MW[ulNoMeasValues];

  if (pMWbuf == NULL ) {
    ulNoMeasValues = 0;
    Serial.println(F("Error in memory allocation!"));
  }
  else {
    Serial.printf("Allocated storage for %d data points.\n", ulNoMeasValues);
  }
  if (ESPHTTPServer._config.LogFreq < 60) ESPHTTPServer._config.LogFreq = 600;
  Serial.printf("log time: %d\n", ESPHTTPServer._config.LogFreq);
  count100ms = millis();
  count1s = millis();
  count10s = millis();
  mainTopic = mainTopic + (String)ESPHTTPServer._config.MQTTTopic + "/" + ESPHTTPServer._config.ClientName;
  Serial.print(F("ende setup"));

}

void loop() {

  if ( (millis() - count100ms) >= 100)
  {
    count100ms = millis();
  }

  if ( (millis() - count1s) >= 1000)
  {
    count1s = millis();
    
    if (millis() - lastLogMillis >= ESPHTTPServer._config.LogFreq * 1000) {
      lastLogMillis = millis();

      //Serial.print(NTP.getUptimeString());

      temp = bme.readTempC();
      pres = bme.readFloatPressure();
      relhum = bme.readFloatHumidity();
      abshum = absFeuchte(temp, relhum, pres);
      Serial.print(F("Time:"));Serial.print(now());Serial.print(" ");Serial.print(NTP.getTimeDateString(now()));
      Serial.print(F(" cidx ")); Serial.print(currentIndex);
      Serial.print(F(" T: ")); Serial.print(temp);
      Serial.print(F(" rHum: ")); Serial.print(relhum);
      Serial.print(F(" aHum: ")); Serial.print(abshum);
      Serial.print(F(" Druck: ")); Serial.println(pres / 100);
//      if (TimeSync) {
//        pMWbuf[currentIndex].timestamp = NTP.getTime();
//      }
//      else
//      {
        pMWbuf[currentIndex].timestamp = now();
      //}
      pMWbuf[currentIndex].temp = round(temp * 100);
      pMWbuf[currentIndex].pressure = round(pres);
      pMWbuf[currentIndex].humid = round(abshum * 100);
      currentIndex = (currentIndex + 1) % ulNoMeasValues;
      

    }
  }

  if ( (millis() - count10s) >= 10000)
  {
     if (WiFi.status() == WL_CONNECTED) {
    handleMQTT();
    if (!TimeSync && NTP.getLastNTPSync() > 0)
    {
      TimeSync = true;
      if (ESPHTTPServer._config.MQTTRefreshInterval > 0)
      {
        MQTTclient.publish((mainTopic + "/Boot").c_str(),
                           (NTP.getTimeStr() + " " + NTP.getDateStr()).c_str());
      }
    }
     }
    //print to console:
//    for (int i = 1; i <= ulNoMeasValues; i++) {
//      int idx = (i + currentIndex) % ulNoMeasValues;
//      //Serial.print(idx);
//      yield();
//      temp = pMWbuf[idx].temp;
//      pres = pMWbuf[idx].pressure;
//      abshum = pMWbuf[idx].humid;
//      // Serial.print(NTP.getTimeDateString(pMWbuf[idx].timestamp));
//      Serial.printf("i=%d idx=%d time=%ul.", i, idx, pMWbuf[idx].timestamp);
//      Serial.print(F("m Temp: ")); Serial.print(temp / 100);
//      //Serial.print(F(" Hum: ")); Serial.print(relhum);
//      Serial.print(F(" absHum: ")); Serial.print(abshum / 100);
//      Serial.print(F(" Druck: ")); Serial.println(pres / 100);
//    }

    
    count10s = millis();
  }
}

void handleMQTT() {
   if (WiFi.status() != WL_CONNECTED) {
    return;    
   }

  if (ESPHTTPServer._config.MQTTRefreshInterval == 0) return;
  if (!MQTTclient.connected()) {
    Serial.println(F("try to connect mqtt Client"));
    if (!reconnect()) return;
  }
  MQTTclient.loop();
  if (millis() - lastMQTTloop > ESPHTTPServer._config.MQTTRefreshInterval * 1000)
  {
    lastMQTTloop = millis();

    if (second() != lastsecond)
    { // do refresh
      //catch F-in and copy counter to values, shoudl be 1000ms interupd, but not simple on esp286
      //Fcount();

      lastsecond = second();
      if (second() % 2 == 0)
      {
        if (ESPHTTPServer._config.MQTTRefreshInterval > 0)
        {
          //mqttcounter = 0;
          //coldstart = false;
          //check value for MQTT publish

          if ((ESPHTTPServer._config.DeviceMode & ModePinA0) == ModePinA0)
          {
            int A0 = analogRead(A0);
            if (lastA0 != A0 && updateCounter[APINUPDATECOUNTER] == 0)
            {
              updateCounter[APINUPDATECOUNTER] = ESPHTTPServer._config.MQTTRefreshInterval;
              lastA0 = A0;
              String topic = mainTopic + "/" + ApinTopic;
              MQTTclient.publish(topic.c_str(), String(A0).c_str(), 1);
            }
          }

          String topic = mainTopic + "/TempC";
          MQTTclient.publish(topic.c_str(), String(temp).c_str(), 1);

          topic = mainTopic + "/RelHumidity";
          MQTTclient.publish(topic.c_str(), String(relhum).c_str(), 1);
          topic = mainTopic + "/AbsHumidity";
          MQTTclient.publish(topic.c_str(), String(abshum).c_str(), 1);

          topic = mainTopic + "/Pressure";
          MQTTclient.publish(topic.c_str(), String(pres/100).c_str(), 1);
        }


      }
    }
  }
}
