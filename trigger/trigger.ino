#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const String deviceId = "";
const String FIREBASE_PROJECTID = "";
const String FIREBASE_APIKEY = "";
const char *ssid = "";
const char *password = "";

// TODO - Consider cleaner pins such as 4, 5, 6, 7, 10
const int LED_PIN = 2;
const int BUTTON_PIN = 3;
const int BUZZER_PIN = 4;

const int BUZZER_TONE_CLICK = 1500; // 3000; //AL. TODO - uncomment
const int BUZZER_DURATION = 200;

const int DEBOUNCE_TIME = 50;
const int INPUT_TIMEOUT = 900;
const int POST_RETRY_INTERVAL = 250;

const int UNDO_HOLD_THRESHOLD = 2000;
const int SWITCH_TEAM_HOLD_THRESHOLD = 5000;

const int WIFI_RETRY_INTERVAL = 5000;

static bool wasConnected = false;
unsigned long lastWiFiAttempt = 0;

WiFiClientSecure client;
HTTPClient http;

String payload = "";
const int payloadBufferSize = 256;

String FIRESTORE_URL = 
  "https://firestore.googleapis.com/v1/projects/" + 
  FIREBASE_PROJECTID + 
  "/databases/(default)/documents/courts/events?key=" + 
  FIREBASE_APIKEY;

typedef const char *EVENT;
const EVENT EVENT_POINT_TEAM_A = "POINT_TEAM_A";
const EVENT EVENT_POINT_TEAM_B = "POINT_TEAM_B";
const EVENT EVENT_UNDO = "UNDO";
const EVENT EVENT_SWITCH_TEAM = "SWITCH_TEAM";

// TODO - try and persist this to device.
char currentTeam = 'A';

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
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION * 10);
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
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION * 1.5);
    delay(BUZZER_DURATION / 2);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION * 1.5);
    break;
  }

  case SWITCH_TEAM: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK * 2, BUZZER_DURATION * 2);
    break;
  }

  default:
    break;
  }
}

void ensureWiFi() {
  bool isConnected = WiFi.status() == WL_CONNECTED;

  if (!isConnected && wasConnected) {
    playSound(NO_WIFI);
  }

  wasConnected = isConnected;

  if (isConnected) return;

  unsigned long now = millis();
  if (now - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = now;
    WiFi.begin(ssid, password);
  }
}

void sendEvent(EVENT event) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // AL.
  // TODO - revise these fields to match website's minimal payload. 
  payload = "";
  payload += "{ \"fields\": {";
  
  payload += "\"event\": {\"stringValue\": \"";
  payload += event;
  payload += "\"},";
  
  payload += "\"deviceId\": {\"stringValue\": \"";
  payload += deviceId;
  payload += "\"}";

  payload += "} }";

  // retry once
  for (int i = 0; i < 2; i++) {
    client.setInsecure();

    http.begin(client, FIRESTORE_URL);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(payload);
    http.end();

    if (code >= 200 && code < 300) {
      return;
    }

    delay(POST_RETRY_INTERVAL);
  }

  playSound(HTTP_POST_FAILED);
}

void addPoint() {
  playSound(ADD_POINT);
  sendEvent(currentTeam == 'A' ? EVENT_POINT_TEAM_A : EVENT_POINT_TEAM_B);
  delay(INPUT_TIMEOUT);
}

void switchTeam() {
  currentTeam = (currentTeam == 'A') ? 'B' : 'A';
  sendEvent(EVENT_SWITCH_TEAM);
}

void undo() { sendEvent(EVENT_UNDO); }

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  playSound(STARTUP);

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