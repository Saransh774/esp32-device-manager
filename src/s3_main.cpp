#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// REPLACE with the MAC Address of your ESP32-C3
uint8_t c3Address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// GitHub repo details
const char* firmware_url = "https://raw.githubusercontent.com/YOUR_USER/YOUR_REPO/main/firmware.bin";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

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
    ws.textAll((char*)msg->payload);
  }
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; background-color: #f4f4f4; margin: 0; padding: 20px; }
    .card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); margin-bottom: 20px; max-width: 400px; margin-left: auto; margin-right: auto; }
    h2 { color: #333; }
    .status { font-size: 24px; font-weight: bold; color: #007bff; }
    button { padding: 10px 20px; font-size: 16px; border: none; border-radius: 5px; cursor: pointer; background: #28a745; color: white; margin: 10px; }
    button.off { background: #dc3545; }
    input[type=number] { padding: 5px; width: 60px; font-size: 16px; }
  </style>
</head>
<body>
  <h2>ESP-NOW Dashboard</h2>
  <div class="card">
    <h3>Sensor Data</h3>
    <p>Distance: <span id="distance" class="status">0</span> cm</p>
    <p>Relay Status: <span id="relay-status" class="status">OFF</span></p>
  </div>
  <div class="card">
    <h3>Relay Control</h3>
    <button id="toggle-btn" onclick="toggleRelay()">Toggle Relay</button>
    <p>Auto Mode: <input type="checkbox" id="auto-mode" onchange="updateAutoMode()"> </p>
    <p>Threshold: <input type="number" id="threshold" value="20" onchange="updateThreshold()"> cm</p>
  </div>
  <div class="card">
    <h3>Firmware Update (ESP-NOW)</h3>
    <button onclick="triggerUpdate()">Flash ESP32-C3</button>
    <p id="update-status"></p>
  </div>
  <script>
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;
    window.addEventListener('load', initWebSocket);

    function initWebSocket() {
      websocket = new WebSocket(gateway);
      websocket.onmessage = (event) => {
        var data = JSON.parse(event.data);
        document.getElementById('distance').innerHTML = data.distance;
        document.getElementById('relay-status').innerHTML = data.relay ? "ON" : "OFF";
        document.getElementById('relay-status').className = data.relay ? "status" : "status off";
        document.getElementById('auto-mode').checked = data.auto;
        document.getElementById('threshold').value = data.threshold;
      };
    }

    function toggleRelay() {
      websocket.send(JSON.stringify({relay: !document.getElementById('relay-status').innerHTML.includes("OFF")}));
    }

    function updateAutoMode() {
      websocket.send(JSON.stringify({auto: document.getElementById('auto-mode').checked}));
    }

    function updateThreshold() {
      websocket.send(JSON.stringify({threshold: parseInt(document.getElementById('threshold').value)}));
    }

    function triggerUpdate() {
      document.getElementById('update-status').innerHTML = "Downloading and flashing...";
      fetch('/update').then(response => response.text()).then(data => {
        document.getElementById('update-status').innerHTML = data;
      });
    }
  </script>
</body>
</html>
)rawliteral";

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    EspNowMsg msg;
    msg.type = CMD_JSON;
    strncpy((char*)msg.payload, (char*)data, sizeof(msg.payload));
    esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
  }
}

void setup() {
  Serial.begin(115200);

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

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_DATA) handleWebSocketMessage(arg, data, len);
  });
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Update process started on S3...");
    
    HTTPClient http;
    http.begin(firmware_url);
    if (http.GET() == HTTP_CODE_OK) {
      int len = http.getSize();
      WiFiClient * stream = http.getStreamPtr();
      
      EspNowMsg msg;
      msg.type = OTA_START;
      msg.totalSize = len;
      esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
      delay(500); // Give C3 time to prepare

      int totalRead = 0;
      while (http.connected() && (totalRead < len)) {
        if (stream->available()) {
          msg.type = OTA_DATA;
          msg.len = stream->readBytes(msg.payload, sizeof(msg.payload));
          esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
          totalRead += msg.len;
          delay(10); // Small delay to prevent ESP-NOW buffer overflow
        }
      }
      
      msg.type = OTA_END;
      esp_now_send(c3Address, (uint8_t *)&msg, sizeof(msg));
      Serial.println("OTA send finished.");
    }
    http.end();
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();
}
