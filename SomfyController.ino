#include <WiFi.h>
#include <LittleFS.h>
#include "ConfigSettings.h"
#include "Network.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "Somfy.h"
#include "MQTT.h"

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
Network net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Startup/Boot....");
  settings.begin();
  WiFi.persistent(false);
  Serial.print("WiFi Mode: ");
  Serial.println(WiFi.getMode());
  Serial.println("Mounting File System...");
  if(LittleFS.begin()) Serial.println("File system mounted successfully");
  else Serial.println("Error mounting file system");
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  delay(10);
  Serial.println();
  webServer.startup();
  webServer.begin();
  delay(1000);
  net.setup();  
  somfy.begin();
}

void loop() {
  // put your main code here, to run repeatedly:
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) ESP.restart();
  net.loop();
  somfy.loop();
  if(WiFi.status() == WL_CONNECTED) {
    webServer.loop();
    sockEmit.loop();
  }
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    net.end();
    ESP.restart();
  }
}
