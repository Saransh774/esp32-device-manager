#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <NewPing.h>
#include <Update.h>

// --- MAC ADDRESS UTILITY ---
// To find your MAC address, upload this and check Serial:
// void setup() { Serial.begin(115200); WiFi.mode(WIFI_STA); Serial.println(WiFi.macAddress()); }
// void loop() {}
// ----------------------------

// REPLACE with the MAC Address of your ESP32-S3
uint8_t s3Address[] = {0x7C, 0x9E, 0xBD, 0xED, 0x9A, 0x21};

// Pins for XIAO ESP32-C3
#define TRIGGER_PIN  4  // D2
#define ECHO_PIN     5  // D3
#define RELAY_PIN    6  // D4
#define MAX_DISTANCE 400

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

bool autoMode = true;
int thresholdDistance = 20;
bool relayState = false;
unsigned long lastSensorRead = 0;
const int sensorInterval = 1000;

// ESP-NOW Protocol Types
enum MsgType { CMD_JSON, OTA_START, OTA_DATA, OTA_END };

typedef struct {
  MsgType type;
  uint32_t totalSize; // For OTA_START
  uint16_t len;       // For OTA_DATA
  uint8_t payload[200];
} EspNowMsg;

void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  EspNowMsg *msg = (EspNowMsg *)incomingData;

  switch (msg->type) {
    case CMD_JSON: {
      JsonDocument doc;
      deserializeJson(doc, (char*)msg->payload);
      if (doc.containsKey("relay")) {
        relayState = doc["relay"];
        autoMode = false;
        digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
      }
      if (doc.containsKey("auto")) autoMode = doc["auto"];
      if (doc.containsKey("threshold")) thresholdDistance = doc["threshold"];
      break;
    }

    case OTA_START:
      Serial.println("OTA Start: " + String(msg->totalSize));
      if (!Update.begin(msg->totalSize)) {
        Serial.println("Update.begin failed: " + String(Update.getError()));
      }
      break;

    case OTA_DATA:
      if (Update.write(msg->payload, msg->len) != msg->len) {
        Serial.println("Update.write failed");
      }
      break;

    case OTA_END:
      if (Update.end(true)) {
        Serial.println("OTA Success. Rebooting...");
        ESP.restart();
      } else {
        Serial.println("Update.end failed: " + String(Update.getError()));
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.mode(WIFI_STA);
  // Channel MUST match the S3 (set after S3 connects to WiFi)
  // For initial pairing, we can force a channel or let it scan.
  // We'll set it in the loop once we know what channel S3 is on.
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, s3Address, 6);
  peerInfo.channel = 0; // Use current channel
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer S3");
  }

  Serial.println("ESP32-C3 ESP-NOW Worker Started.");
}

void loop() {
  if (millis() - lastSensorRead >= sensorInterval) {
    lastSensorRead = millis();
    int distance = sonar.ping_cm();
    
    if (autoMode && distance > 0) {
      relayState = (distance < thresholdDistance);
      digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
    }

    JsonDocument doc;
    doc["distance"] = distance;
    doc["relay"] = relayState;
    doc["auto"] = autoMode;
    doc["threshold"] = thresholdDistance;

    String output;
    serializeJson(doc, output);
    
    EspNowMsg msg;
    msg.type = CMD_JSON;
    strncpy((char*)msg.payload, output.c_str(), sizeof(msg.payload));
    
    esp_now_send(s3Address, (uint8_t *)&msg, sizeof(msg));
  }
}
