#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_wifi.h"

const bool DEBUG = true;
const bool UNDERCLOCK = true;

//AL. //TODO - store these via captive portal on initial boot.
const String DEVICEID = "proto_trigger_1";
const String FIREBASE_PROJECT = "punto-8888";
const String FIREBASE_APIKEY = "AIzaSyA6sA_c3yNUZvvo_dZanhydLn7jXl-55hU";
const char* WIFI_NAME = "OwlBird";
const char* WIFI_PASSWORD = "0823082006";

const int LED_PIN = 2;
const int BUTTON_PIN = 3;
const int BUZZER_PIN = 4;

const int BUZZER_TONE_CLICK = 3000;
const int BUZZER_DURATION = 200;

const int DEBOUNCE_TIME = 1000;
const int UNDO_HOLD_THRESHOLD = 2000;
const int SWITCH_TEAM_HOLD_THRESHOLD = 5000;

const int WIFI_RETRY_INTERVAL = 5000;

static bool wasConnected = false;
unsigned long lastWiFiAttempt = 0;

WiFiClientSecure client;
HTTPClient https;
const uint16_t HTTPS_CONNECT_TIMEOUT = 2000;
const uint16_t HTTPS_RESPONSE_TIMEOUT = 3000;
const int POST_RETRY_INTERVAL = 250;
const int PAYLOAD_BUFFER_SIZE = 256;

String REGION = "africa-south1";

String POSTEVENT_ENDPOINT = 
  "https://" + REGION + "-" + FIREBASE_PROJECT + ".cloudfunctions.net/postEvent";

typedef const char *EVENT;
const EVENT EVENT_POINT_TEAM_A = "POINT_TEAM_A";
const EVENT EVENT_POINT_TEAM_B = "POINT_TEAM_B";
const EVENT EVENT_UNDO = "UNDO";

//AL. //TODO - persist this to device.
char currentTeam = 'A';

static unsigned long lastProcessedPressTime = 0;

enum SOUNDS {
  SND_CONNECTED,
  SND_NO_WIFI,
  SND_HTTP_POST_FAILED,

  SND_ADD_POINT,
  SND_UNDO,
  SND_SWITCH_TEAM,
};

void playSound(SOUNDS sound) {
  return;
  switch (sound) {
  case SND_CONNECTED: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 4, BUZZER_DURATION / 4);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 3, BUZZER_DURATION / 3);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION / 2);
    break;
  }

  case SND_NO_WIFI: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION / 3);        
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 3, BUZZER_DURATION / 3);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 4, BUZZER_DURATION / 4);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION / 3);        
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 3, BUZZER_DURATION / 3);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 4, BUZZER_DURATION / 4);
    break;
  }

  case SND_HTTP_POST_FAILED: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    delay(BUZZER_DURATION * 2);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    delay(BUZZER_DURATION * 2);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION * 2);
    break;
  }

  case SND_ADD_POINT: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK, BUZZER_DURATION);
    break;
  }

  case SND_UNDO: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK * 0.75, BUZZER_DURATION * 1);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK * 0.65, BUZZER_DURATION * 1.5);
    break;
  }

  case SND_SWITCH_TEAM: {
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 2, BUZZER_DURATION);
    delay(BUZZER_DURATION);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION);
    break;
  }

  default:
    break;
  }
}

void ensureWiFi() {
  bool isConnected = WiFi.status() == WL_CONNECTED;
  bool failed = WiFi.status() == WL_CONNECT_FAILED;

  //Prolly still trying to connect atm...
  if (!isConnected && !failed) {
    return;
  }

  if (WIFI_NAME == "") {
    return;
  }

  if (isConnected && !wasConnected) {
    log("Successfully Connected.");
    playSound(SND_CONNECTED);

    log("Enabling WiFi modem sleep");
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  }
  else if (!isConnected && wasConnected) {
    log("CONNECTION LOST.");
    playSound(SND_NO_WIFI);

    log("Disabling WiFi modem sleep");
    esp_wifi_set_ps(WIFI_PS_NONE);
  }

  wasConnected = isConnected;

  if (isConnected) return;

  unsigned long now = millis();
  if (now - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = now;
    log("Attempting to connect to SSID: " + String(WIFI_NAME));
    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  }
}

void sendEvent(EVENT event) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  char payload[PAYLOAD_BUFFER_SIZE];
  snprintf(
    payload, sizeof(payload), 
    "{\"deviceId\":\"%s\",\"eventType\":\"%s\"}", 
    DEVICEID.c_str(), event
    );

  // retry once
  for (int i = 0; i < 2; i++) {
    client.setInsecure();

    https.begin(client, POSTEVENT_ENDPOINT);
    https.setConnectTimeout(HTTPS_CONNECT_TIMEOUT);
    https.setTimeout(HTTPS_RESPONSE_TIMEOUT);
    https.addHeader("Content-Type", "application/json");

    log(
      "\nEvent: \n" + String(event) +
      "\nEndpoint: \n" + POSTEVENT_ENDPOINT + 
      "\nPayload: \n" + payload
    );

    int code = https.POST(payload);

    log(    
      "\nResponse Code: \n" + String(code) + 
      "\nResponse Message: \n" + https.getString()
    );

    https.end();

    if (code >= 200 && code < 300) {
      return;
    }

    delay(POST_RETRY_INTERVAL);
  }

  playSound(SND_HTTP_POST_FAILED);
}

void addPoint() {
  playSound(SND_ADD_POINT);
  sendEvent(currentTeam == 'A' ? EVENT_POINT_TEAM_A : EVENT_POINT_TEAM_B);
}

void switchTeam() {
  currentTeam = (currentTeam == 'A') ? 'B' : 'A';
}

void undo() { sendEvent(EVENT_UNDO); }

void log(String s) {
  if (DEBUG) {
    Serial.println(s);
  }  
}

void setup() {
  if (UNDERCLOCK) {
    setCpuFrequencyMhz(80);
  }
  
  if (DEBUG) {
    Serial.begin (115200);
  }
  log("\n\nSetting Up...");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

  log("Setup Complete.");
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

    unsigned long now = millis();

    if (now - lastProcessedPressTime < DEBOUNCE_TIME) {
      lastButtonState = currentButtonState;
      return;
    }

    if (WiFi.status() != WL_CONNECTED) {
      playSound(SND_NO_WIFI);
      lastButtonState = LOW;
      return;
    }
    
    isPressing = true;
    pressStartTime = now;
    soundUndoPlayed = false;
    soundSwitchTeamPlayed = false;
    digitalWrite(LED_PIN, HIGH);
  }

  // Button held
  if (isPressing && currentButtonState == LOW) {
    unsigned long duration = millis() - pressStartTime;

    if (duration >= SWITCH_TEAM_HOLD_THRESHOLD && !soundSwitchTeamPlayed) {
      playSound(SND_SWITCH_TEAM);
      soundSwitchTeamPlayed = true;
    } else if (duration >= UNDO_HOLD_THRESHOLD && !soundUndoPlayed &&
               !soundSwitchTeamPlayed) {
      playSound(SND_UNDO);
      soundUndoPlayed = true;
    }
  } 

  // Button released
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    digitalWrite(LED_PIN, LOW);
    isPressing = false;
    unsigned long releasedTime = millis();
    lastProcessedPressTime = releasedTime;
    unsigned long duration = releasedTime - pressStartTime;

    if (duration >= SWITCH_TEAM_HOLD_THRESHOLD) {
      switchTeam();
    } else if (duration >= UNDO_HOLD_THRESHOLD) {
      undo();
    } else {
      addPoint();
    }
  }

  lastButtonState = currentButtonState;
}