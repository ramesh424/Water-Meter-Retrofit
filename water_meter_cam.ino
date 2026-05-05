/*
  ESP32-CAM (AI Thinker)
  Auto SD save + Auto Upload + UDP Auto-Discovery
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "FS.h"
#include "SD_MMC.h"
#include "esp_timer.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ====================== Wi-Fi Configuration ======================
const char* ssid     = "ECE-A212";         // 🔹 Your Wi-Fi name
const char* password = "vnrvjiet@321";     // 🔹 Your Wi-Fi password

// ====================== Web Login ======================
const char* www_username = "admin";
const char* www_password = "1234";


// ====================== Camera Pins (AI Thinker) ======================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ====================== Globals ======================
WebServer server(80);
WiFiUDP udp;
String serverIP = "";
unsigned long lastCapture = 0;
unsigned long captureDelay = 5000; // ms (default 5 seconds)

// ====================== UDP Auto-Discovery ======================
void discoverServer() {
  Serial.println("🔍 Discovering server...");
  udp.begin(8889); // random outgoing port
  udp.beginPacket("255.255.255.255", 8888); // broadcast message
  udp.print("ESP_DISCOVER");
  udp.endPacket();

  unsigned long start = millis();
  while (millis() - start < 5000) { // wait up to 5s
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char buffer[64];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      buffer[len] = 0;
      if (strncmp(buffer, "SERVER_IP:", 10) == 0) {
        serverIP = String(buffer + 10);
        Serial.print("✅ Found server IP: ");
        Serial.println(serverIP);
        break;
      }
    }
    delay(100);
  }
  udp.stop();
  if (serverIP == "") Serial.println("⚠️ No server found — will retry later.");
}

// ====================== Upload Function ======================
void sendToServer(camera_fb_t *fb) {
  if (serverIP == "") {
    discoverServer(); // try again if not found
    if (serverIP == "") {
      Serial.println("❌ No server IP found, skipping upload.");
      return;
    }
  }

  HTTPClient http;
  WiFiClient client;
  String url = "http://" + serverIP + ":8081";
  http.begin(client, url);
  http.addHeader("Content-Type", "image/jpeg");
  int httpCode = http.POST((uint8_t*)fb->buf, fb->len);

  if (httpCode > 0) {
    Serial.printf("✅ Uploaded %d bytes to %s (HTTP %d)\n", fb->len, serverIP.c_str(), httpCode);
  } else {
    Serial.printf("❌ Upload failed (%d)\n", httpCode);
  }
  http.end();
}

// ====================== Capture & Save ======================
void captureAndSave(bool upload = true) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // Save to SD card
  fs::FS &fs = SD_MMC;
  String path = "/photo_" + String(millis()) + ".jpg";
  File file = fs.open(path, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.println("💾 Saved to SD: " + path);
  } else {
    Serial.println("⚠️ Failed to write to SD card");
  }

  // Upload to PC
  if (upload) sendToServer(fb);

  esp_camera_fb_return(fb);
}

// ====================== Auto Capture ======================
void autoCapture() {
  unsigned long now = millis();
  if (now - lastCapture >= captureDelay) {
    lastCapture = now;
    captureAndSave(true);
  }
}

// ====================== Web Handlers ======================
void handleRoot() {
  if (!server.authenticate(www_username, www_password))
    return server.requestAuthentication();

  String html = R"rawliteral(
  <html><head><title>ESP32-CAM Dashboard</title>
  <style>body{font-family:Arial;background:#222;color:#fff;text-align:center;}
  button,input{padding:8px;margin:6px;border-radius:8px;font-size:16px;}</style></head>
  <body>
  <h2>ESP32-CAM Web Dashboard</h2>
  <img src="/capture" width="80%%"><br><br>
  <form action="/setDelay" method="get">
  Auto Save Delay (ms): <input name="delay" type="number" value="%DELAY%">
  <input type="submit" value="Set">
  </form>
  <p><b>Local IP:</b> %IP%<br><b>Server:</b> %SERVER%</p>
  </body></html>
  )rawliteral";

  html.replace("%DELAY%", String(captureDelay));
  html.replace("%IP%", WiFi.localIP().toString());
  html.replace("%SERVER%", (serverIP == "" ? "Not found" : serverIP));
  server.send(200, "text/html", html);
}

void handleCapture() {
  if (!server.authenticate(www_username, www_password))
    return server.requestAuthentication();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera failed");
    return;
  }

  server.sendHeader("Content-Type", "image/jpeg");
  server.send(200, "image/jpeg", "");
  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleSetDelay() {
  if (!server.authenticate(www_username, www_password))
    return server.requestAuthentication();

  if (server.hasArg("delay")) captureDelay = server.arg("delay").toInt();
  server.sendHeader("Location", "/");
  server.send(303);
}

// ====================== Setup ======================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println();

  // Camera setup
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera init failed");
    return;
  }

  // SD card
  if (!SD_MMC.begin()) Serial.println("⚠️ SD Card Mount Failed");
  else Serial.println("💾 SD Card ready");

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("✅ Wi-Fi connected, IP: ");
  Serial.println(WiFi.localIP());

  // Discover PC
  discoverServer();

  // Web endpoints
  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/setDelay", handleSetDelay);
  server.begin();
  Serial.println("🌐 Web server started");
}

// ====================== Loop ======================
void loop() {
  server.handleClient();
  autoCapture();
}
