#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// REPLACE with the MAC Address of your ESP32-C3
uint8_t c3Address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// GitHub repo details
const char* firmware_url = "https://raw.githubusercontent.com/YOUR_USER/YOUR_REPO/main/firmware.bin";

AsyncWebServer server(80);
AsyncEventSource events("/events");

// ESP-NOW Protocol Types
enum MsgType { CMD_JSON, OTA_START, OTA_DATA, OTA_END };

typedef struct {
  MsgType type;
  uint32_t totalSize;
  uint16_t len;
  uint8_t payload[200];
} EspNowMsg;

void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  EspNowMsg *msg = (EspNowMsg *)incomingData;
  if (msg->type == CMD_JSON) {
    // Inject RSSI into JSON for dashboard
    JsonDocument doc;
    deserializeJson(doc, (char*)msg->payload);
    doc["rssi"] = WiFi.RSSI();
    String output;
    serializeJson(doc, output);
    events.send(output.c_str(), "message", millis());
  }
}

void sendOTAProgress(int percent, String message) {
  JsonDocument doc;
  doc["percent"] = percent;
  doc["message"] = message;
  String output;
  serializeJson(doc, output);
  events.send(output.c_str(), "ota_progress", millis());
}

void setup() {
  Serial.begin(115200);

  if(!LittleFS.begin(true)){
    Serial.println("LittleFS Mount Failed");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected. Channel: " + String(WiFi.channel()));

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, c3Address, 6);
  peerInfo.channel = WiFi.channel();
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer C3");
  }

  // Web Server Routes
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.addHandler(&events);

  server.on("/relay", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    EspNowMsg msg;
    msg.type = CMD_JSON;
    memcpy(msg.payload, data, min(len, sizeof(msg.payload)));
    msg.payload[min(len, sizeof(msg.payload)-1)] = 0;
    esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/ota/update", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Update process started...");
    
    // Perform OTA in a separate task or after response
    HTTPClient http;
    http.begin(firmware_url);
    if (http.GET() == HTTP_CODE_OK) {
      int len = http.getSize();
      WiFiClient * stream = http.getStreamPtr();
      
      EspNowMsg msg;
      msg.type = OTA_START;
      msg.totalSize = len;
      esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
      delay(500);

      int totalRead = 0;
      while (http.connected() && (totalRead < len)) {
        if (stream->available()) {
          msg.type = OTA_DATA;
          msg.len = stream->readBytes(msg.payload, sizeof(msg.payload));
          esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
          totalRead += msg.len;
          
          int percent = (totalRead * 100) / len;
          if (percent % 5 == 0) {
            sendOTAProgress(percent, "Flashing...");
          }
          delay(10);
        }
      }
      
      msg.type = OTA_END;
      esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
      sendOTAProgress(100, "Rebooting C3...");
    } else {
      sendOTAProgress(0, "Download Failed");
    }
    http.end();
  });

  server.begin();
}

void loop() {}
