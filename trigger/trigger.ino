#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const String deviceId = "proto_trigger_1";

// TODO - make sure not using RX pins. Consider pins 5, 18, 19, 21, 22, 23
const int LED_PIN = 2;
const int BUTTON_PIN = 3;
const int BUZZER_PIN = 4;

const int BUZZER_TONE_CLICK = 1500; // 3000; //AL. TODO - uncomment
const int BUZZER_DURATION = 200;

const int DEBOUNCE_TIME = 50;
const int INPUT_TIMEOUT = 900;

const int UNDO_HOLD_THRESHOLD = 2000;
const int SWITCH_TEAM_HOLD_THRESHOLD = 5000;

const char *ssid = "";
const char *password = "";
const int WIFI_RETRY_INTERVAL = 5000;

unsigned long lastWiFiAttempt = 0;

const char *FIREBASE_PROJECTID = "";
const char *FIREBASE_APIKEY = "";

WiFiClientSecure client;
HTTPClient http;
bool httpInitialized = false;

String payload = "";
const int payloadBufferSize = 256;

String currentTeam = "A"; // TODO - try and persist this to device.

typedef const char *EVENT;
const EVENT EVENT_POINT_TEAM_A = "POINT_TEAM_A";
const EVENT EVENT_POINT_TEAM_B = "POINT_TEAM_B";
const EVENT EVENT_UNDO = "UNDO";
const EVENT EVENT_SWITCH_TEAM = "SWITCH_TEAM";

enum SOUNDS {
  STARTUP,
  NO_WIFI,
  HTTP_POST_FAILED,
  ADD_POINT,
  UNDO,
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
  case UNDO: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION);
    break;
  }

  case SWITCH_TEAM: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK * 2, BUZZER_DURATION);
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

void sendEvent(EVENT event) {
  if (WiFi.status() != WL_CONNECTED) {
    playSound(NO_WIFI);
    return;
  }

  if (!httpInitialized)
    initHttp();

  // AL.
  // TODO - revise the fields.
  payload = "";
  payload += "{ \"fields\": {";
  payload += "\"event\": {\"stringValue\": \"" + String(event) + "\"},";
  payload += "\"deviceId\": {\"stringValue\": \"" + String(deviceId) + "\"}";
  payload += "} }";

  // retry once
  for (int i = 0; i < 2; i++) {
    int code = http.POST(payload);
    if (code >= 200 && code < 300) {
      return;
    }

    // http.end(); //AL. TODO - investigate .setReuse() and .end()
    httpInitialized = false;
    initHttp();
  }

  playSound(HTTP_POST_FAILED);
}

void addPoint() {
  sendEvent(currentTeam == "A" ? EVENT_POINT_TEAM_A : EVENT_POINT_TEAM_B);
  delay(INPUT_TIMEOUT);
}

void switchTeam() {
  currentTeam = currentTeam == "A" ? "B" : "A";
  sendEvent(EVENT_SWITCH_TEAM);
}

void undo() { sendEvent(EVENT_UNDO); }

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.begin(ssid, password);

  payload.reserve(payloadBufferSize);
}

void loop() {
  ensureWiFi();

  static bool lastButtonState = HIGH;
  static unsigned long pressStartTime = 0;
  static bool soundUndoPlayed = false;
  static bool soundSwitchTeamPlayed = false;
  static bool isPressing = false;

  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Button pressed
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    isPressing = true;
    pressStartTime = millis();
    soundUndoPlayed = false;
    soundSwitchTeamPlayed = false;
    digitalWrite(LED_PIN, HIGH);
    playSound(ADD_POINT);
  }

  // Button held
  if (isPressing && currentButtonState == LOW) {
    unsigned long duration = millis() - pressStartTime;

    if (duration >= SWITCH_TEAM_HOLD_THRESHOLD && !soundSwitchTeamPlayed) {
      playSound(SWITCH_TEAM);
      soundSwitchTeamPlayed = true;
    } else if (duration >= UNDO_HOLD_THRESHOLD && !soundUndoPlayed &&
               !soundSwitchTeamPlayed) {
      playSound(UNDO);
      soundUndoPlayed = true;
    }
  }

  // Button released
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    digitalWrite(LED_PIN, LOW);
    isPressing = false;
    unsigned long duration = millis() - pressStartTime;

    if (duration >= SWITCH_TEAM_HOLD_THRESHOLD) {
      switchTeam();
    } else if (duration >= UNDO_HOLD_THRESHOLD) {
      undo();
    } else {
      addPoint();
    }
  }

  lastButtonState = currentButtonState;
  delay(DEBOUNCE_TIME);
}