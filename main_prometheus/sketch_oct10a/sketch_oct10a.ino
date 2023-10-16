/*
This sketch demonstrates reading a thermistor and posting the temperature to a RESTful API with a GET.
The REST API is custom code running on a raspberry pi listening on port 8010
See github project https://github.com/bernpuc/particle for details.
*/
#include "arduino_secrets.h"
#include <ESP8266WiFiMulti.h>
#include <ArduinoHttpClient.h>

// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000      
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950
// the value of the 'other' resistor
#define SERIESRESISTOR 10000
// Difference in temperature at 0C
#define CALIBRATION_OFFSET 2.7
// enable serial port diagnostic output
#define DEBUG 0

// Global variables
unsigned long temploopStart;
const int analogPin = A0;
float refVoltage = 3.3;
const int led = 2;
ESP8266WiFiMulti WiFiMulti;
WiFiClient wifi;

// Function declarations
float read_thermistor();
float VtoT(int voltage);
void sendData_prometheus(float temperature);


void setup() {
  Serial.begin(115200);

  // Reading temperature setup
  pinMode(analogPin, INPUT);
  temploopStart = 0;
  
  // Posting temperature setup
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(SECRET_SSID1, SECRET_SSID1_PWORD);
  WiFiMulti.addAP(SECRET_SSID2, SECRET_SSID2_PWORD);
    while (WiFiMulti.run() != WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
    delay(250);
    Serial.print('.');
  }
  Serial.print("setup() done connected to ssid ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());

  // Turn on the Indicator LED
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
}

void loop() {
  static float temperature;   // Store most recent temperature reading
  
  // Read the temperature from the thermistor
  if (temploopStart == 0 || (millis() - temploopStart) > 5000) {
    temperature = read_thermistor();
    sendData_prometheus(temperature);
    temploopStart = millis();
  }
}

float read_thermistor() {

  #define NUM_READS 10
  int reading = 0;
  for (int idx=0; idx<NUM_READS; idx++) {
    reading += analogRead(analogPin);
  }
  reading = reading/NUM_READS;

  return VtoT(reading);
}

// Converts the digital reading from the A2D input into a temperature
float VtoT(int reading) {
  float mVolts = (refVoltage/1023.0)*reading*1000;
  float resistance = SERIESRESISTOR/((1023.0/reading)-1);
  float tempC;
  float tempF;
  tempC = resistance / THERMISTORNOMINAL;       // (R/Ro)
  tempC = log(tempC);                     // ln(R/Ro)
  tempC /= BCOEFFICIENT;                        // 1/B * ln(R/Ro)
  tempC += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  tempC = 1.0 / tempC;                    // Invert
  tempC -= 273.15;                              // convert absolute temp to C
  tempC += CALIBRATION_OFFSET;                  // calibrated offset
  tempF = (tempC * (9.0 / 5.0)) + 32;

  if (DEBUG) {
    Serial.printf("%0.1f C, %0.1f mV, %0.1f ohms\n", tempC, mVolts, resistance);
  }
  return tempF;
}

void sendData_prometheus(float temperature)
{
  if ((WiFiMulti.run() == WL_CONNECTED)) {

    char apiString[64] = {0};
    sprintf(apiString, "/update?%s=%0.2f", "temperature", temperature);

    HttpClient client = HttpClient(wifi, SECRET_RELAY_API_ADDR, SECRET_RELAY_API_PORT);
    if (client.get(apiString)){
      Serial.printf("There was an error in client.get(%s)\n", apiString);
    }
    else {
      int statusCode = client.responseStatusCode();
      Serial.print("HTTP Status code: ");
      Serial.println(statusCode);
    }

  }
  else {
    Serial.println("WiFi disconnected");
  }
}