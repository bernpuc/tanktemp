#include <Arduino.h>
#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// For thingspeak connection
#include <ArduinoHttpClient.h>

#include "certs.h"
#include "arduino_secrets.h"

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
unsigned long grafanaloop;
unsigned long thingspeakloop;
const int analogPin = A0;
float refVoltage = 3.3;
ESP8266WiFiMulti WiFiMulti;
WiFiClient wifi;
const int led = 2;

// Function declarations
float read_thermistor();
float VtoT(int voltage);
void grafanaPost(float temperature);
void thingspeakGet(float temperature);


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
  grafanaloop = 0;
  thingspeakloop = 0;

  // Turn on the Indicator LED
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
}

void loop() {
  static float temperature;   // Store most recent temperature reading
  
  // Read the temperature from the thermistor
  if (temploopStart == 0 || (millis() - temploopStart) > 1000) {
    temperature = read_thermistor();
    temploopStart = millis();
  }

  // push to grafana channel every 5 minutes
  // if (grafanaloop == 0 || (millis() - grafanaloop) > (1000 * 60 * 5)) {
  //   grafanaPost(temperature);
  //   grafanaloop = millis();
  // }

  // push to thingspeak channel every 15 minutes
  if (thingspeakloop == 0 || (millis() - thingspeakloop) > (1000 * 60 * 15)) {
    thingspeakGet(temperature);
    thingspeakloop = millis();
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

void grafanaPost(float temperature) {
    //HttpClient client_g = HttpClient(wifi, "api.terramisha.com", 80);
    //StaticJsonDocument<200> doc;
    // Add values in the document
    //
    //doc["temperatureF"] = temperature;
    //String requestBody;
    //serializeJson(doc, requestBody);
    //String contentType = "application/json";
    //client_g.post("/oscarapi/all", contentType, requestBody);

    // read the status code and body of the response
    //int statusCode = client_g.responseStatusCode();
    //String response = client_g.responseBody();

    //Serial.print("grafana POST Status code: ");
    //Serial.println(statusCode);
    //Serial.print("Response: ");
    //Serial.println(response);
    if ((WiFiMulti.run() == WL_CONNECTED)) {

    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);

    client->setFingerprint(fingerprint_sni_cloudflaressl_com);
    // Or, if you happy to ignore the SSL certificate, then use the following line instead:
    // client->setInsecure();

    HTTPClient https;

    Serial.print("[HTTPS] begin...\n");
    if (https.begin(*client, grafana_host, grafana_port, "/oscarapi/all")) {  // HTTPS
      StaticJsonDocument<200> doc;
      doc["temperatureF"] = temperature;
      String requestBody;
      serializeJson(doc, requestBody);

      Serial.print("[HTTPS] POST...\n");
      // start connection and send HTTP header
      https.addHeader("Content-Type", "application/json");
      int httpCode = https.POST(requestBody);

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("[HTTPS] POST... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();
          Serial.println(payload);
        }
      } else {
        Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }

      https.end();
    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
    }
  }
}

void thingspeakGet(float temperature) {

  char apiString[64] = {0};
  sprintf(apiString, "/update?api_key=%s&%s=%0.2f", SECRET_THINGSPEAK_API_KEY, SECRET_CHANNEL_WATER_TEMP, temperature);

  HttpClient client = HttpClient(wifi, "api.thingspeak.com", 80);

  if (client.get(apiString)){
    Serial.printf("There was an error in client.get(%s)\n", apiString);
  }
  else {
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();
    Serial.print("thingspeak GET Status code: ");
    Serial.println(statusCode);
    Serial.print("Response: ");
    Serial.println(response);
    Serial.printf("%s - %0.2f\n", response, temperature);
  }
}
