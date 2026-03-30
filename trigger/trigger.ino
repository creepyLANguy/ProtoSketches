#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const String deviceId = "proto_trigger_1";

const int LED_PIN = 2;
const int BUTTON_PIN = 3;
const int BUZZER_PIN = 4;

const int BUZZER_TONE_CLICK = 1500; // 3000; //AL. TODO - uncomment
const int BUZZER_DURATION = 200;

const int TIMEOUT = 900;

const char *ssid = "";
const char *password = "";
const int WIFI_RETRY_INTERVAL = 5000;

unsigned long lastWiFiAttempt = 0;

const char *FIREBASE_PROJECTID = "";
const char *FIREBASE_APIKEY = "";

WiFiClientSecure client;
HTTPClient http;
bool httpInitialized = false;

String currentTeam = "A"; // TODO - implement team switching;

enum SOUNDS {
  STARTUP,
  NO_WIFI,
  HTTP_POST_FAILED,
  ADD_POINT,
  SWITCH_TEAM,
};

void playSound(SOUNDS sound) {

  switch (sound) {
  case STARTUP: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK, BUZZER_DURATION);
    break;
  }
  case NO_WIFI: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION * 6);
    break;
  }
  case HTTP_POST_FAILED: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    break;
  }
  case ADD_POINT: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK, BUZZER_DURATION);
    break;
  }

  case SWITCH_TEAM: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK, BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK, BUZZER_DURATION);
    break;
  }
  default:
    break;
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  playSound(NO_WIFI);
  unsigned long now = millis();
  if (now - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = now;
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

void initHttp() {
  if (httpInitialized)
    return;

  client.setInsecure(); // skip cert validation for embedded

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECTID;
  url += "/databases/(default)/documents/courts/events?key=";
  url += FIREBASE_APIKEY;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setReuse(true);

  httpInitialized = true;
}

void sendEvent(String eventName) {
  if (WiFi.status() != WL_CONNECTED) {
    playSound(NO_WIFI);
  }

  if (!httpInitialized)
    initHttp();

  // AL.
  // TODO - revise the fields.
  String payload = "{ \"fields\": {";
  payload += "\"event\": {\"stringValue\": \"" + eventName + "\"},";
  payload += "\"deviceId\": {\"stringValue\": \"" + String(deviceId) + "\"},";
  payload += "\"team\": {\"stringValue\": \"" + String(currentTeam) + "\"},";
  payload += "\"ts\": {\"integerValue\": \"" + String(millis()) + "\"}";
  payload += "} }";

  // retry once
  for (int i = 0; i < 2; i++) {
    int code = http.POST(payload);
    if (code > 0) {
      return;
    }

    http.end();
    httpInitialized = false;
    initHttp();
  }

  playSound(HTTP_POST_FAILED);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.begin(ssid, password);
}

void loop() {
  ensureWiFi();

  bool pressed = digitalRead(BUTTON_PIN) == LOW;

  if (pressed) {
    digitalWrite(LED_PIN, HIGH);
    playSound(USER_CLICKED_POINT);

    sendEvent("POINT_TEAM_" + currentTeam);

    delay(TIMEOUT);
    digitalWrite(LED_PIN, LOW);
  }
}