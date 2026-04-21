#include "esp_mac.h"
#include "esp_wifi.h"
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const bool DEBUG = true;
const bool UNDERCLOCK = false;

const String deviceSKU = "Pulse Mini";

const String FIREBASE_PROJECT = "[PROJECT_ID]";
const String FIREBASE_APIKEY = "[API_KEY]";

const int LED_PIN = 2;
const int BUTTON_PIN = 3;
const int BUZZER_PIN = 4;

Preferences preferences;
DNSServer dnsServer;
WebServer server(80);

struct WiFiCreds {
  String ssid;
  String pass;
};

IPAddress apIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

const int MAX_WIFI_NETWORKS = 5;
WiFiCreds savedWiFi[MAX_WIFI_NETWORKS];
int wifiCount = 0;

bool isConfigMode = false;
unsigned long lastStatusFlash = 0;
bool statusLedState = false;

String DEVICEID = "";

char currentTeam = 'A';

typedef const char *EVENT;

const int DEBOUNCE_TIME = 50;
const int PRESS_COOLDOWN = 500;

const int UNDO_HOLD_THRESHOLD = 2000;
const int SWITCH_TEAM_HOLD_THRESHOLD = 5000;
const int FACTORY_RESET_HOLD_THRESHOLD = 15000;

const int WIFI_RETRY_INTERVAL = 5000;
static bool wasConnected = false;
unsigned long lastWiFiAttempt = 0;

WiFiClientSecure client;

const uint16_t HTTPS_CONNECT_TIMEOUT = 2000;
const uint16_t HTTPS_RESPONSE_TIMEOUT = 3000;
const int POST_RETRY_INTERVAL = 250;
const int PAYLOAD_BUFFER_SIZE = 256;

String REGION = "africa-south1";

String POSTEVENT_ENDPOINT = "https://" + REGION + "-" + FIREBASE_PROJECT +
                            ".cloudfunctions.net/postEvent";

const EVENT EVENT_POINT_TEAM_A = "POINT_TEAM_A";
const EVENT EVENT_POINT_TEAM_B = "POINT_TEAM_B";
const EVENT EVENT_UNDO = "UNDO";

void loadWiFiList() {
  preferences.begin("wifi-store", true);
  wifiCount = preferences.getInt("count", 0);
  for (int i = 0; i < wifiCount; i++) {
    savedWiFi[i].ssid = preferences.getString(("s" + String(i)).c_str(), "");
    savedWiFi[i].pass = preferences.getString(("p" + String(i)).c_str(), "");
  }
  preferences.end();
}

void saveWiFi(String ssid, String pass) {
  // Check if already exists
  int existingIdx = -1;
  for (int i = 0; i < wifiCount; i++) {
    if (savedWiFi[i].ssid == ssid) {
      existingIdx = i;
      break;
    }
  }

  // Shift for MRU
  if (existingIdx != -1) {
    for (int i = existingIdx; i > 0; i--) {
      savedWiFi[i] = savedWiFi[i - 1];
    }
  } else {
    int limit =
        (wifiCount < MAX_WIFI_NETWORKS) ? wifiCount : MAX_WIFI_NETWORKS - 1;
    for (int i = limit; i > 0; i--) {
      savedWiFi[i] = savedWiFi[i - 1];
    }
    if (wifiCount < MAX_WIFI_NETWORKS)
      wifiCount++;
  }

  savedWiFi[0].ssid = ssid;
  savedWiFi[0].pass = pass;

  // Persist
  preferences.begin("wifi-store", false);
  preferences.putInt("count", wifiCount);
  for (int i = 0; i < wifiCount; i++) {
    preferences.putString(("s" + String(i)).c_str(), savedWiFi[i].ssid);
    preferences.putString(("p" + String(i)).c_str(), savedWiFi[i].pass);
  }
  preferences.end();
}

void loadCurrentTeam() {
  preferences.begin("device-prefs", true);
  currentTeam = (char)preferences.getInt("team", 'A');
  preferences.end();
}

void saveCurrentTeam(char team) {
  preferences.begin("device-prefs", false);
  preferences.putInt("team", (int)team);
  preferences.end();
}

// ==========================
// 🔊 SOUND DEFINITIONS
// ==========================

enum SOUNDS {
  SND_CONNECTED,
  SND_NO_WIFI,
  SND_HTTP_POST_FAILED,
  SND_ADD_POINT,
  SND_UNDO,
  SND_SWITCH_TEAM,
  SND_FACTORY_RESET,
};

struct SoundStep {
  double freq;
  double duration;
  double pauseAfter;
};

const int MAX_SOUND_STEPS = 10;

struct Sound {
  SoundStep steps[MAX_SOUND_STEPS];
  int length;
};

void playSound(SOUNDS sound);
void startSound(Sound& sound);
void updateSound();

const int BUZZER_TONE_CLICK = 3000;
const int BUZZER_DURATION = 200;

Sound SND_ADD_POINT_OBJ = {{
  {BUZZER_TONE_CLICK, BUZZER_DURATION, 0}}, 
  1};

Sound SND_UNDO_OBJ = {{
  {BUZZER_TONE_CLICK * 0.75, BUZZER_DURATION * 0.75, 50},
  {BUZZER_TONE_CLICK * 0.65, BUZZER_DURATION, 0}},
  2};

Sound SND_SWITCH_TEAM_OBJ = {
  {{BUZZER_TONE_CLICK / 2, BUZZER_DURATION, 50},
  {BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION / 1.5, 50},
  {BUZZER_TONE_CLICK / 2, BUZZER_DURATION, 50},
  {BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION / 1.5, 0}},
  4};

Sound SND_CONNECTED_OBJ = {{
  {BUZZER_TONE_CLICK / 4, 80, 50},
  {BUZZER_TONE_CLICK / 3, 100, 50},
  {BUZZER_TONE_CLICK / 2, 120, 0}},
  3};

Sound SND_NO_WIFI_OBJ = {{
  {BUZZER_TONE_CLICK / 2, 80, 50},
  {BUZZER_TONE_CLICK / 3, 80, 50},
  {BUZZER_TONE_CLICK / 4, 80, 50},
  {BUZZER_TONE_CLICK / 2, 80, 50},
  {BUZZER_TONE_CLICK / 3, 80, 50},
  {BUZZER_TONE_CLICK / 4, 80, 0}},
  6};

Sound SND_HTTP_FAIL_OBJ = {{
  {BUZZER_TONE_CLICK / 2, 200, 200},
  {BUZZER_TONE_CLICK / 2, 200, 200},
  {BUZZER_TONE_CLICK / 2, 400, 0}},
  3};

Sound SND_FACTORY_RESET_OBJ = {{
  {BUZZER_TONE_CLICK, 50, 20},
  {BUZZER_TONE_CLICK * 0.8, 50, 20},
  {BUZZER_TONE_CLICK * 0.6, 50, 20},
  {BUZZER_TONE_CLICK * 0.4, 50, 20},
  {BUZZER_TONE_CLICK * 0.2, 50, 20},
  {BUZZER_TONE_CLICK * 0.1, 100, 0}},
  6};

Sound currentSound;
int soundIndex = 0;
unsigned long soundStart = 0;
bool isPlayingSound = false;

void playSound(SOUNDS sound) {
  switch (sound) {
  case SND_CONNECTED:
    startSound(SND_CONNECTED_OBJ);
    break;
  case SND_NO_WIFI:
    startSound(SND_NO_WIFI_OBJ);
    break;
  case SND_HTTP_POST_FAILED:
    startSound(SND_HTTP_FAIL_OBJ);
    break;
  case SND_ADD_POINT:
    startSound(SND_ADD_POINT_OBJ);
    break;
  case SND_UNDO:
    startSound(SND_UNDO_OBJ);
    break;
  case SND_SWITCH_TEAM:
    startSound(SND_SWITCH_TEAM_OBJ);
    break;
  case SND_FACTORY_RESET:
    startSound(SND_FACTORY_RESET_OBJ);
    break;
  }
}

void startSound(Sound& sound) {
  currentSound = sound;
  soundIndex = 0;
  soundStart = millis();
  isPlayingSound = true;
  tone(BUZZER_PIN, (int)sound.steps[0].freq, (int)sound.steps[0].duration);
}

void updateSound() {
  if (!isPlayingSound)
    return;

  unsigned long now = millis();
  SoundStep step = currentSound.steps[soundIndex];

  if (now - soundStart >= (step.duration + step.pauseAfter)) {
    soundIndex++;

    if (soundIndex >= currentSound.length) {
      isPlayingSound = false;
      noTone(BUZZER_PIN);
      return;
    }

    soundStart = now;
    SoundStep next = currentSound.steps[soundIndex];
    tone(BUZZER_PIN, (int)next.freq, (int)next.duration);
  }
}

// ==========================
// WIFI + API
// ==========================

bool tryConnect(String ssid, String pass) {
  log("Connecting to: " + ssid);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    digitalWrite(LED_PIN, (attempts % 2 == 0));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    log("Connected!");
    digitalWrite(LED_PIN, LOW);
    saveWiFi(ssid, pass);
    return true;
  }

  log("Failed to connect.");
  return false;
}

bool autoConnect() {
  for (int i = 0; i < wifiCount; i++) {
    if (tryConnect(savedWiFi[i].ssid, savedWiFi[i].pass)) {
      return true;
    }
  }
  return false;
}

void ensureWiFi() {
  bool isConnected = WiFi.status() == WL_CONNECTED;

  if (isConnected && !wasConnected) {
    log("WiFi Connected");
    playSound(SND_CONNECTED);
    digitalWrite(LED_PIN, LOW); // Ensure LED is OFF on connection
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  } else if (!isConnected && wasConnected) {
    log("Lost connection");
    playSound(SND_NO_WIFI);
    esp_wifi_set_ps(WIFI_PS_NONE);
  }

  wasConnected = isConnected;

  if (isConnected)
    return;

  unsigned long now = millis();
  if (now - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = now;
    autoConnect();
  }
}

void handleRoot() {
  String html =
      "<!DOCTYPE html><html lang='en'><head>"
      "<meta charset='UTF-8'><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<title>Padel Push - WiFi Setup</title>"
      "<style>"
      "body { background: #0a0e17; color: #ffffff; font-family: -apple-system, "
      "BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; "
      "display: flex; flex-direction: column; align-items: center; "
      "justify-content: center; min-height: 100vh; margin: 0; padding: 20px; "
      "text-align: center; }"
      "h1 { color: #f7ff00; font-weight: 800; "
      "margin-bottom: 30px; font-size: 2.5rem; text-transform: uppercase; }"
      "h2 { color: #ffffff; font-weight: 400; font-size: 1.2rem; margin-top: "
      "-25px; "
      "margin-bottom: 30px; opacity: 0.8; }"
      ".card { background: rgba(255, 255, 255, 0.05); backdrop-filter: "
      "blur(10px); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: "
      "20px; padding: 30px; width: 100%; max-width: 400px; box-shadow: 0 20px "
      "40px rgba(0,0,0,0.4); }"
      ".net-item { display: flex; justify-content: space-between; align-items: "
      "center; padding: 18px 20px; border-bottom: 1px solid rgba(255,255,255,0.08); "
      "cursor: pointer; transition: all 0.2s ease; background: rgba(255,255,255,0.02); "
      "position: relative; overflow: hidden; }"
      ".net-item:last-child { border-bottom: none; }"
      ".net-item:hover { background: rgba(0, 242, 255, 0.08); }"
      ".net-item::after { content: '›'; color: #00f2ff; font-size: 1.5rem; "
      "margin-left: 10px; opacity: 0.5; transition: transform 0.2s; }"
      ".net-item:hover::after { transform: translateX(5px); opacity: 1; }"
      ".ssid { font-weight: 600; font-size: 1.1rem; }"
      ".rssi { color: #00f2ff; font-family: monospace; }"
      "form { display: none; flex-direction: column; gap: 15px; margin-top: "
      "20px; padding: 25px; }"
      "input { background: rgba(255, 255, 255, 0.1); border: 1px solid "
      "rgba(255, 255, 255, 0.2); border-radius: 10px; padding: 12px; color: "
      "#fff; font-size: 1rem; outline: none; transition: border-color 0.2s; }"
      "input:focus { border-color: #f7ff00; }"
      "button { background: #f7ff00; color: #0a0e17; border: none; "
      "border-radius: 10px; padding: 15px; font-weight: 800; text-transform: "
      "uppercase; cursor: pointer; transition: transform 0.1s, opacity 0.2s; }"
      "button:hover { opacity: 0.9; transform: translateY(-2px); }"
      "button:active { transform: translateY(0); }"
      ".footer { margin-top: 30px; font-size: 0.8rem; opacity: 0.5; "
      "text-transform: uppercase; letter-spacing: 0.2em; }"
      "</style></head><body>"
      "<h1>Padel Push</h1>"
      "<h2>Setup Portal</h2>"
      "<div class='card' style='padding: 0; overflow: hidden; border-radius: 12px;'>"
      "<div id='net-list'>";

  WiFi.disconnect(); // Ensure we are not trying to connect while scanning
  int n = WiFi.scanNetworks();
  if (n == 0) {
    html += "<p style='padding: 20px;'>No networks found.</p>";
  } else {
    for (int i = 0; i < n; ++i) {
      html += "<div class='net-item' onclick='selectNet(\"" + WiFi.SSID(i) +
              "\")'>"
              "<span class='ssid'>" +
              WiFi.SSID(i) +
              "</span>"
              "<span class='rssi'>" +
              String(WiFi.RSSI(i)) +
              " dBm</span>"
              "</div>";
    }
  }

  html += "<div class='net-item' onclick='manualNet()' style='background: rgba(247, 255, 0, 0.05); border-top: 1px solid rgba(247, 255, 0, 0.2);'>"
          "<span class='ssid' style='color: #f7ff00;'>+ Join Hidden Network</span>"
          "</div>"
          "</div>"
          "<form id='config-form' action='/connect' method='POST'>"
          "<input type='text' id='ssid' name='ssid' placeholder='Network Name' style='display:none; margin-bottom: 10px;'>"
          "<div style='margin-bottom: 20px; font-weight: 600; color: #00f2ff;' id='selected-ssid'></div>"
          "<input type='password' name='pass' placeholder='Password'>"
          "<button type='submit'>Connect</button>"
          "<button type='button' style='margin-top: 10px; background: rgba(255,255,255,0.1); color: #fff;' onclick='showList()'>Back</button>"
          "</form>"
          "</div>"
          "<div class='footer'>© 2026 Padel Push - All Rights Reserved</div>"
          "<script>"
          "function selectNet(ssid) {"
          "  document.getElementById('ssid').value = ssid;"
          "  document.getElementById('ssid').style.display = 'none';"
          "  document.getElementById('selected-ssid').innerText = 'Network: ' + ssid;"
          "  document.getElementById('selected-ssid').style.display = 'block';"
          "  document.getElementById('net-list').style.display = 'none';"
          "  document.getElementById('config-form').style.display = 'flex';"
          "}"
          "function manualNet() {"
          "  document.getElementById('ssid').value = '';"
          "  document.getElementById('ssid').style.display = 'block';"
          "  document.getElementById('selected-ssid').style.display = 'none';"
          "  document.getElementById('net-list').style.display = 'none';"
          "  document.getElementById('config-form').style.display = 'flex';"
          "}"
          "function showList() {"
          "  document.getElementById('net-list').style.display = 'block';"
          "  document.getElementById('config-form').style.display = 'none';"
          "}"
          "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  pass.trim();

  // Show connecting page
  String html =
      "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
      "<style>body { background: #0a0e17; color: #fff; font-family: sans-serif; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; text-align: center; }"
      "h2 { color: #00f2ff; }</style></head><body>"
      "<div>"
      "<h2>Connecting to " + ssid + "...</h2>"
      "<p>Please wait while we connect your device.</p>"
      "</div></body></html>";
  server.send(200, "text/html", html);

  delay(1000); // small delay before attempting connection
  if (tryConnect(ssid, pass)) {
    // Show success page
    html =
        "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<style>body { background: #0a0e17; color: #f7ff00; font-family: sans-serif; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; text-align: center; }"
        "h2 { font-size: 2rem; color: #f7ff00; }</style></head><body>"
        "<div>"
        "<h2>✅ Connected Successfully!</h2>"
        "<p>The device is now connected to " + ssid + ".</p>"
        "<p>This window will close automatically.</p>"
        "</div>"
        "<script>setTimeout(function(){ window.close(); }, 3000);</script>"
        "</body></html>";
    server.send(200, "text/html", html);
    
    log("Connected! Closing AP mode in 2 seconds...");
    delay(2000); // Let user see the success message and hear the sound

    // Close AP mode and switch to STA
    isConfigMode = false;
    WiFi.softAPdisconnect(true);
    log("Switching to STA mode");
  } else {
    // Failed to connect
    log("Failed to connect, showing failure page");
    WiFi.disconnect(); // Explicitly disconnect to clean up
    playSound(SND_NO_WIFI);

    html =
        "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<style>body { background: #0a0e17; color: #fff; font-family: sans-serif; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; text-align: center; padding: 20px; }"
        "h2 { color: #ff3b3b; font-size: 2rem; }"
        ".btn { background: #f7ff00; color: #0a0e17; border: none; border-radius: 10px; padding: 15px 30px; font-weight: 800; text-transform: uppercase; cursor: pointer; text-decoration: none; display: inline-block; margin-top: 20px; }"
        "</style></head><body>"
        "<div>"
        "<h2>❌ Connection Failed</h2>"
        "<p>Could not connect to <b>" + ssid + "</b>.</p>"
        "<p>Please check the password and try again.</p>"
        "<a href='/' class='btn'>Try Again</a>"
        "</div>"
        "</body></html>";
    server.send(200, "text/html", html);
  }
}

void handleRedirect() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void startCaptivePortal() {
  log("Starting Captive Portal...");
  isConfigMode = true;
  WiFi.mode(WIFI_AP);

  // Set static AP IP
  if (!WiFi.softAPConfig(apIP, gateway, subnet)) {
    log("Failed to configure AP IP!");
  }

  String shortId;
  if (DEVICEID.length() >= 4) {
    shortId = DEVICEID.substring(DEVICEID.length() - 4);
  } else {
    shortId = DEVICEID; // fallback if somehow shorter than 4 chars
  }
  String apName = "Padel Push | " + deviceSKU + " - " + shortId;
  log(apName);
  WiFi.softAP(apName, "", 1, false, 4);

  // Start DNS server to redirect all requests to ESP
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);

  // Standard captive portal routes - Redirect to root to trigger portal popup
  server.on("/generate_204", handleRedirect);
  server.on("/hotspot-detect.html", handleRedirect);
  server.on("/connecttest.txt", handleRedirect);
  server.on("/ncsi.txt", handleRedirect);

  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();

  log(WiFi.softAPIP().toString());

  playSound(SND_NO_WIFI);
}

void sendEvent(EVENT event) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  char payload[PAYLOAD_BUFFER_SIZE];
  snprintf(payload, sizeof(payload),
           "{\"deviceId\":\"%s\",\"eventType\":\"%s\"}", DEVICEID.c_str(),
           event);

  // retries once
  for (int i = 0; i < 2; i++) {
    client.setInsecure();

    HTTPClient https;
    https.setConnectTimeout(HTTPS_CONNECT_TIMEOUT);
    https.setTimeout(HTTPS_RESPONSE_TIMEOUT);
    https.begin(client, POSTEVENT_ENDPOINT);
    https.addHeader("Content-Type", "application/json");

    log("\nEvent: \n" + String(event) + "\nEndpoint: \n" + POSTEVENT_ENDPOINT +
        "\nPayload: \n" + payload);

    int code = https.POST(payload);

    log("\nResponse Code: \n" + String(code) + "\nResponse Message: \n" +
        https.getString());

    https.end();

    if (code >= 200 && code < 300)
      return;

    delay(POST_RETRY_INTERVAL);
  }

  playSound(SND_HTTP_POST_FAILED);
}

// ==========================
// ACTIONS
// ==========================

void addPoint() {
  playSound(SND_ADD_POINT);
  sendEvent(currentTeam == 'A' ? EVENT_POINT_TEAM_A : EVENT_POINT_TEAM_B);
}

void switchTeam() {
  currentTeam = (currentTeam == 'A') ? 'B' : 'A';
  saveCurrentTeam(currentTeam);
}

void undo() { sendEvent(EVENT_UNDO); }

void factoryReset() {
  log("FACTORY RESET INITIATED");

  preferences.begin("wifi-store", false);
  preferences.clear();
  preferences.end();

  preferences.begin("device-prefs", false);
  preferences.clear();
  preferences.end();

  wifiCount = 0;
  currentTeam = 'A';

  WiFi.disconnect(true, true);
  log("Storage cleared. Restarting...");
  delay(1000);
  ESP.restart();
}

void log(String s) {
  if (DEBUG)
    Serial.println(s);
}

// ==========================
// SETUP
// ==========================

void setup() {
  if (DEBUG)
    Serial.begin(115200);

  log("\n\nSetting Up...");

  if (UNDERCLOCK)
    setCpuFrequencyMhz(80);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  loadWiFiList();
  loadCurrentTeam();

  // DEVICEID = WiFi.macAddress();
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_BASE);
  char baseMacChr[18] = {0};
  sprintf(baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1],
          baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  DEVICEID = String(baseMacChr);
  DEVICEID.replace(":", ""); // Clean device ID

  log("Device ID: " + DEVICEID);

  if (!autoConnect()) {
    startCaptivePortal();
  } else {
    log("Setup Complete (Connected).");
  }
}

// ==========================
// LOOP
// ==========================

void loop() {
  if (isConfigMode) {
    dnsServer.processNextRequest();
    server.handleClient();

    // Flash LED in config mode
    unsigned long now = millis();
    if (now - lastStatusFlash > 500) {
      lastStatusFlash = now;
      statusLedState = !statusLedState;
      digitalWrite(LED_PIN, statusLedState);
    }
  } else {
    ensureWiFi();
  }

  updateSound();

  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastDebounceTime = 0;

  static unsigned long pressStartTime = 0;
  static unsigned long lastPressTime = 0;

  static bool isPressing = false;
  static bool soundUndoPlayed = false;
  static bool soundSwitchPlayed = false;
  static bool soundResetPlayed = false;

  bool reading = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (reading != lastReading) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > DEBOUNCE_TIME) {

    if (reading != stableState) {
      stableState = reading;

      // PRESS
      if (stableState == LOW) {

        if (now - lastPressTime < PRESS_COOLDOWN)
          return;

        if (WiFi.status() != WL_CONNECTED) {
          playSound(SND_NO_WIFI);
          return;
        }

        isPressing = true;
        pressStartTime = now;
        soundUndoPlayed = false;
        soundSwitchPlayed = false;
        soundResetPlayed = false;

        digitalWrite(LED_PIN, HIGH);
      }

      // RELEASE
      else {
        digitalWrite(LED_PIN, LOW);

        if (!isPressing)
          return;

        isPressing = false;
        lastPressTime = now;

        unsigned long duration = now - pressStartTime;

        if (duration >= FACTORY_RESET_HOLD_THRESHOLD) {
          factoryReset();
        } else if (duration >= SWITCH_TEAM_HOLD_THRESHOLD) {
          switchTeam();
        } else if (duration >= UNDO_HOLD_THRESHOLD) {
          undo();
        } else {
          addPoint();
        }
      }
    }
  }

  lastReading = reading;

  // HOLD FEEDBACK
  if (isPressing && stableState == LOW) {
    unsigned long duration = now - pressStartTime;

    if (duration >= FACTORY_RESET_HOLD_THRESHOLD && !soundResetPlayed) {
      playSound(SND_FACTORY_RESET);
      soundResetPlayed = true;
    } else if (duration >= SWITCH_TEAM_HOLD_THRESHOLD && !soundSwitchPlayed &&
               !soundResetPlayed) {
      playSound(SND_SWITCH_TEAM);
      soundSwitchPlayed = true;
    } else if (duration >= UNDO_HOLD_THRESHOLD && !soundUndoPlayed &&
               !soundSwitchPlayed && !soundResetPlayed) {
      playSound(SND_UNDO);
      soundUndoPlayed = true;
    }
  }
}