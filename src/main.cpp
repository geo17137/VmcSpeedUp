// Author : Daniel Tschirhart
// Date   : 18/4/2022
// Version 1.5
// Mise à jour par OTA
// REM
// Le niveau de commande actif est LOW
// La led de l'esp01 s'allume lorsque le courtier est connecté
// et s'éteint dans le cas contraire
// Le programme gère la reconnexion automatique en cas de redémarrage
// du courtier

#include <Arduino.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "../secret/password.h"

/*
secret/password.h
#define SSID     "xxxxx"
#define PASSWORD    "xxxxx"
#define MQTT_SERVER "xxxxxxx"
#define MQTT_PORT xxxx
#define MQTT_USER "xxxxxxx"
#define MQTT_PASSWORD "xxxxxxxx"
#define HOSTNAME "xxxxxxxx"
*/


#define PUB_GPIO0_STATUS "vmc_board/status"
#define SUB_ACTION       "vmc_board/action"

const char* version      = "2024.09.20";
const char* ssid         = SSID;
const char* password     = PASSWORD;
const char* mqttServer   = MQTT_SERVER;
const int   mqttPort     = MQTT_PORT;
const char* mqttUser     = MQTT_USER;
const char* mqttPassword = MQTT_PASSWORD;

const char* ON  = "on";
const char* OFF = "off";
const char *GET_STATUS = "get_status";
const char *PUBLISH_STATE_ON =  "pub_on";
const char *PUBLISH_STATE_OFF = "pub_off";
const char *PUBLISH_VERSION =   "pub_version";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
boolean pubOn;
boolean isOn;

#define GPIO2_LED 2
#define GPIO0_RELAY 0

void PubSubCallback(char* topic, byte* payload, unsigned int length);

void initWifiStation() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    pinMode(GPIO2_LED, HIGH);
    Serial.printf("Connection wifi %s failed! Rebooting...", ssid);
    delay(5000);
    ESP.restart();
  }
  WiFi.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() {
    Serial.println("Start ota");
  });

  ArduinoOTA.onEnd([]() {
    ESP.restart();
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

  ArduinoOTA.begin();
  Serial.println(String("\nConnected to the WiFi network (") + ssid + ")" );
  ArduinoOTA.setHostname(HOSTNAME);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void publishState() {
  // Serial.printf("GPIO0_RELAY %d\n", val);
  int val = digitalRead(GPIO0_RELAY);
  mqttClient.publish(PUB_GPIO0_STATUS, val == 0 ? ON : OFF);
}

void initMQTTClient() {
  // Connecting to MQTT server
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(PubSubCallback);
  while (!mqttClient.connected()) {
    Serial.println(String("Connecting to MQTT (") + mqttServer + ")...");

    // Pour un même courtier les clients doivent avoir un id différent
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword)) {
      Serial.println("MQTT client connected");
      digitalWrite(GPIO2_LED, LOW);
      publishState();
    } else {
      Serial.print("\nFailed with state ");
      Serial.println(mqttClient.state());
      digitalWrite(GPIO2_LED, HIGH);
      if (WiFi.status() != WL_CONNECTED) {
        initWifiStation();
      }
      delay(5000);
    }
  }
  mqttClient.subscribe(SUB_ACTION);
}

void setup() {
  Serial.begin(112500);
  pinMode(GPIO0_RELAY, OUTPUT);
  pinMode(GPIO2_LED, OUTPUT);
  pinMode(GPIO2_LED, HIGH);
  digitalWrite(GPIO0_RELAY, HIGH);

  initWifiStation();
  initMQTTClient();
  Serial.println(WiFi.localIP());
}

void loop() {
  static long tps = 0;
  ArduinoOTA.handle();
  if (!mqttClient.connected()) {
    digitalWrite(GPIO2_LED, HIGH);
    initMQTTClient();
  }
  mqttClient.loop();
  // Mettre à jour l'état du relai toutes les 2s
  if (millis() - tps > 2000) {
    tps = millis();
    if (isOn)
      digitalWrite(GPIO0_RELAY, LOW);
    if (pubOn)
      publishState();
  }
}

void PubSubCallback(char* topic, byte* payload, unsigned int length) {

  static String strTopicGpio0Action = SUB_ACTION;
  String strPayload = "";

  static String strON = ON;
  static String strOFF = OFF;
  static String getStatus = GET_STATUS;
  static String publish_on =  PUBLISH_STATE_ON;
  static String publish_off = PUBLISH_STATE_OFF;
  static String publish_version = PUBLISH_VERSION;

  for (unsigned int i = 0; i < length; i++) {
    strPayload += (char)payload[i];
  }
  // Serial.printf("TOPIC %s, Message %s\n", topic, strPayload.c_str());
  if (strTopicGpio0Action == topic) {
    if (strON == strPayload) {
      digitalWrite(GPIO0_RELAY, LOW);
      isOn = true;
      pubOn = true;
      return;
    } 
    if (strOFF == strPayload) {
      digitalWrite(GPIO0_RELAY, HIGH);
      isOn = false;
      pubOn = false;
      mqttClient.publish(PUB_GPIO0_STATUS, OFF);
      return;
    }
    if (getStatus == strPayload) {
      publishState();
      return;
    }
    if (publish_on == strPayload) {
      pubOn = true;
      return;
    }
    if (publish_off == strPayload) {
      pubOn = false;
    }
    if (publish_version == strPayload) {
      mqttClient.publish(PUB_GPIO0_STATUS, version);
    }
  }
}