#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <Adafruit_MCP3008.h>
#include "FS.h"
#include <JeVe_EasyOTA.h>  // https://github.com/jeroenvermeulen/JeVe_EasyOTA/blob/master/JeVe_EasyOTA.h


//******************************************* DEFINITIONS *********************************************//
#define DBG_OUTPUT_PORT Serial


#define TYRE_CIRCUMFERENCE 195 // distance car travels in one wheel rotation, measured in centimeters

// ***************************************** PIN Defines **********************************************//

//digital inputs  GPIO 0 -4 //
// NOTE: GPIO 13-15 (D5-D8) are reserved for SPI communication with MCP3008 ADC
// voltage uses simple divider circuit

#define HIGHBEAM_PIN D0// D0 cannont be used as an interrupt pin but can be used as a GPIO pin
#define LEFTINDICATOR_PIN D1 // GPIO0 (D3)
#define RIGHTINDICATOR_PIN D2 // GPIO5 (D1)
// so now we can use D3 and D4 for interrupts
#define TACHO_PIN     D3 // GPI O2 (D4) Input - Tachometer signal thru schmitt trigger optocoupler.
#define SPEEDO_PIN  D4 //  GPIO4 (D2) physical 10 D6, logical 10
// Analogue (ADC) inputs
#define VOLTS_PIN 0 //0-100
#define FUEL_PIN 1 //0-100
#define OILPRESSURE_PIN 2 //0-80
#define OILTEMPERATURE_PIN 3//0-180

// other definitions used to calculate various Values
#define TACHFILTERSIZE  10// Size of the moving average filter for RPM.
#define SPEEDOFILTERSIZE 5

#define systemLedPin 13

//******************************************* GLOBALS *********************************************//
//************************************* VARIABLES AND CONSTANTS ***************************************//

const char* ssid = "VWDash";
const char* password = "IGiveUp24318";
// OTA values
char* WIFI_SSID = "Gannymede";
char* WIFI_PASSWORD = "IGiveUp24318";
char* ARDUINO_HOSTNAME = "vw_dashboard";
EasyOTA OTA;

Adafruit_MCP3008 adc;
ESP8266WebServer server(80);

// tachometer calculation values
volatile long fTachCounter = 0;
float fTachFrequencyArray[TACHFILTERSIZE];  // moving average filter for the RPM
volatile long fElapsedTime;
int fPreviousTime=millis();
float fThisRPM = 0;
// speedo
volatile int fLastTimeAround=millis();
volatile float fSpeedoKPHArray[SPEEDOFILTERSIZE];
long fFilterSum = 0;
volatile float fThisKph;
//float value=0;
//float fRev=0;

//************************************* END VARIABLES AND CONSTANTS ***************************************//

// ****************************************** METHODS ***************************************//
// ************************************** General methods ***********************************//
void listSPIFFS(){
  DBG_OUTPUT_PORT.println("Listing SPIFFS contents");
  Dir dir = SPIFFS.openDir("");
  while (dir.next()){
    DBG_OUTPUT_PORT.print(dir.fileName());
    DBG_OUTPUT_PORT.print(": \t");
    DBG_OUTPUT_PORT.println(dir.fileSize());
  }
}
// *************************************** Tacho methods ************************************//
  // Tach signal input triggers this interrupt vector on the falling edge of pin D3 //
  void tacho_isr(){
    fTachCounter++;
  }
  // calculate the engine speed using a moving average
  float getRpm(){
    fThisRPM = (fTachCounter/(millis() - fPreviousTime)) * 60000;
                 //saves the current time
    fTachCounter = 0;
    fFilterSum = 0;
    // Shift elements up one, and add the total.
    for ( int i = 0; i < TACHFILTERSIZE - 1 ; i++ ) {
      fTachFrequencyArray[i] = fTachFrequencyArray[i + 1];
      fFilterSum += fTachFrequencyArray[i]; // adding the
    }
    // add the newest reading to the end of the array.
    fTachFrequencyArray[TACHFILTERSIZE - 1] = fThisRPM; // Because the ISR is called @ counterFreq Hz
    fFilterSum += fThisRPM;
    fPreviousTime=millis();
    // now return the adjusted average
    return ( fFilterSum / TACHFILTERSIZE ) * 15 ; //60/4 ppr=15
  }
// **************************************** Tacho methods end *************************************//
// ***************************************** Speedo methods ***************************************//
// speedo signal input triggers this interrupt vector on the rising edge of pin D4 //
void speedo_isr(){
  fElapsedTime = millis() - fLastTimeAround;
  fThisKph = (TYRE_CIRCUMFERENCE/100000)/(fElapsedTime/3600000 );

  for ( int i = 0; i < SPEEDOFILTERSIZE - 1 ; i++ ) {
    fSpeedoKPHArray[i] = fSpeedoKPHArray[i + 1];
  }
  // add the newest reading to the end of the array.
  fSpeedoKPHArray[SPEEDOFILTERSIZE - 1] = fThisKph; // Because the ISR is called @ counterFreq Hz
  fLastTimeAround = millis();
}

float getSpeedOverDistance(){
  float fKphSum = 0;
  for ( int i = 0; i < SPEEDOFILTERSIZE - 1 ; i++ ) {
    fKphSum+=fSpeedoKPHArray[i];
  }
  return fKphSum/SPEEDOFILTERSIZE;
}
// *************************************** Speedo methods end *************************************//
// ******************************************* other methods **************************************//

int getRotation(int reading, int startDegrees, int endDegrees, int startValue, int endValue)
{
    if( reading==0||reading<=startValue)
    {
      return startDegrees;
    }
    else if (reading>=endValue) {
      return endDegrees;
    }
    else
    {
      int totalRotation = endDegrees - startDegrees;
      int totalScale = (endValue - startValue);
      int fReading = (reading - startValue);
      return round(((fReading/totalScale)*totalRotation) + startDegrees);
    }

}

float getTrueVoltage(int adcIn){
  DBG_OUTPUT_PORT.print("voltage in from adc : ");
  DBG_OUTPUT_PORT.println(adcIn);
  float returnVoltage = 20 * adcIn/1024;
  return(returnVoltage);
  DBG_OUTPUT_PORT.print("return voltage");
  DBG_OUTPUT_PORT.println(returnVoltage);
}

//******************************************** getData() *******************************************//
void getData() {
  // NOTE: **************** this is called frequently so keep it lean ******************************//
  // Tachometer values
  detachInterrupt(digitalPinToInterrupt(TACHO_PIN));           //detaches the interrupt while we update the values
    float fRPM=getRpm();
  attachInterrupt(digitalPinToInterrupt(TACHO_PIN),tacho_isr,RISING); // re-attaches the interupt coil goes low on each pulse but input is inverted by the H11L1M
  // Speedo values
  detachInterrupt(digitalPinToInterrupt(SPEEDO_PIN));
  float fKph = getSpeedOverDistance();
  attachInterrupt(digitalPinToInterrupt(SPEEDO_PIN),speedo_isr,FALLING);
  //This is a JSON formatted string that will be served. You can change the values to whatever like.
  // {"data":[{"dataValue":"1024"},{"dataValue":"23"}]} This is essentially what is will output you can add more if you like
  float fTrueVoltage = getTrueVoltage(adc.readADC(VOLTS_PIN));
  float fFuelReading = adc.readADC(FUEL_PIN);
  float fOilPressureReading = adc.readADC(OILPRESSURE_PIN);
  float fOilTempReading = adc.readADC(OILTEMPERATURE_PIN);

  StaticJsonBuffer<2048> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  root["speed"] = getRotation(fKph,-135,135,0,180);
  root["rpm"] = getRotation(fRPM,-135,105,0,8000);

  root["fuel"] = getRotation(fFuelReading,-45,45,0,100);
  root["volts"] = getRotation(fTrueVoltage,-45,45,8,16);
  root["pressure"] = getRotation(fOilPressureReading,-45,45,0,80);
  root["temp"] = getRotation(fOilTempReading,-45,45,60,160);

  root["leftIndicator"] = !digitalRead(LEFTINDICATOR_PIN);
  root["rightIndicator"] = !digitalRead(RIGHTINDICATOR_PIN);
  root["highBeamWarning"] = !digitalRead(HIGHBEAM_PIN);

  root["lowFuelWarning"] = fFuelReading<10;
  root["lowOilWarning"]= fOilPressureReading < 30;
  root["lowBatteryWarning"] = fTrueVoltage < 12;
  root["hotOilWarning"] = fOilTempReading > 200;

  //print the resulting JSON to a String
  String output;
  root.printTo(output);
  server.send(200, "text/html", output);
}

bool createWifiAP() {
  DBG_OUTPUT_PORT.print("Using ssid: ");
  DBG_OUTPUT_PORT.println(ssid);
  DBG_OUTPUT_PORT.print("Using password: ");
  DBG_OUTPUT_PORT.println(password);

  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();

  DBG_OUTPUT_PORT.print("AP IP address: ");
  DBG_OUTPUT_PORT.println(myIP);
  // handlers called from browser to access files in SPIFFS file system on 8266
  server.serveStatic("/", SPIFFS, "/dashboard.html");
  server.serveStatic("/index.html", SPIFFS, "/dashboard.html");
  server.serveStatic("/dashboard.svg", SPIFFS, "/dashboard.svg");

  server.on("/data", getData);

  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");

  return true;
}

void setup() {
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print("\n");
  if (!SPIFFS.begin()) {
    DBG_OUTPUT_PORT.println("Failed to mount file system");
    return;
  }
  if (!createWifiAP()) {
    DBG_OUTPUT_PORT.println("Failed to create WiFi AP");
    return;
  }
  // This callback will be called when JeVe_EasyOTA has anything to tell you.
  OTA.onMessage([](char *message, int line) {
    Serial.println(message);
  });

  OTA.setup(WIFI_SSID, WIFI_PASSWORD, ARDUINO_HOSTNAME);
  // Define the input pins // remember the inputs through the H11L1Ms are inverted
  pinMode( HIGHBEAM_PIN, INPUT_PULLUP);
  pinMode( LEFTINDICATOR_PIN, INPUT_PULLUP);
  pinMode( RIGHTINDICATOR_PIN, INPUT_PULLUP);
  // initialise the Interrupt handling on the input pins
  fTachCounter = 0;
  attachInterrupt(digitalPinToInterrupt(TACHO_PIN),tacho_isr,RISING);
  attachInterrupt(digitalPinToInterrupt(SPEEDO_PIN),speedo_isr,FALLING);
  // start the mcp3008 ADC
  adc.begin();
}

void loop() {
  OTA.loop();
  server.handleClient();
  yield();
}
