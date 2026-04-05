#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_wifi.h"

const bool DEBUG = true;
const bool UNDERCLOCK = true;

// CONFIG
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

const int DEBOUNCE_TIME = 50;
const int PRESS_COOLDOWN = 500;

const int UNDO_HOLD_THRESHOLD = 2000;
const int SWITCH_TEAM_HOLD_THRESHOLD = 5000;

const int WIFI_RETRY_INTERVAL = 5000;

static bool wasConnected = false;
unsigned long lastWiFiAttempt = 0;

WiFiClientSecure client;

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

char currentTeam = 'A';

enum SOUNDS {
  SND_CONNECTED,
  SND_NO_WIFI,
  SND_HTTP_POST_FAILED,
  SND_ADD_POINT,
  SND_UNDO,
  SND_SWITCH_TEAM,
};

// ==========================
// 🔊 NON-BLOCKING SOUND ENGINE
// ==========================

struct SoundStep {
  double freq;
  double duration;
  double pauseAfter;
};

#define MAX_STEPS 10

struct Sound {
  SoundStep steps[MAX_STEPS];
  int length;
};

Sound currentSound;
int soundIndex = 0;
unsigned long soundStart = 0;
bool isPlayingSound = false;

void startSound(Sound sound) {
  currentSound = sound;
  soundIndex = 0;
  soundStart = millis();
  isPlayingSound = true;

  tone(BUZZER_PIN, (int)sound.steps[0].freq, (int)sound.steps[0].duration);
}

void updateSound() {
  if (!isPlayingSound) return;

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
// 🔊 SOUND DEFINITIONS
// ==========================

Sound SND_ADD_POINT_OBJ = {{
  {BUZZER_TONE_CLICK, BUZZER_DURATION, 0}
}, 1};

Sound SND_UNDO_OBJ = {{
  {BUZZER_TONE_CLICK * 0.75, BUZZER_DURATION * 0.75, 50},
  {BUZZER_TONE_CLICK * 0.65, BUZZER_DURATION, 0}
}, 2};

Sound SND_SWITCH_TEAM_OBJ = {{
  {BUZZER_TONE_CLICK / 2, BUZZER_DURATION, 50},
  {BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION / 1.5, 50},
  {BUZZER_TONE_CLICK / 2, BUZZER_DURATION, 50},
  {BUZZER_TONE_CLICK / 1.5, BUZZER_DURATION / 1.5, 0}
}, 4};

Sound SND_CONNECTED_OBJ = {{
  {BUZZER_TONE_CLICK / 4, 80, 50},
  {BUZZER_TONE_CLICK / 3, 100, 50},
  {BUZZER_TONE_CLICK / 2, 120, 0}
}, 3};

Sound SND_NO_WIFI_OBJ = {{
  {BUZZER_TONE_CLICK / 2, 80, 50},
  {BUZZER_TONE_CLICK / 3, 80, 50},
  {BUZZER_TONE_CLICK / 4, 80, 50},
  {BUZZER_TONE_CLICK / 2, 80, 50},
  {BUZZER_TONE_CLICK / 3, 80, 50},
  {BUZZER_TONE_CLICK / 4, 80, 0}
}, 6};

Sound SND_HTTP_FAIL_OBJ = {{
  {BUZZER_TONE_CLICK / 2, 200, 200},
  {BUZZER_TONE_CLICK / 2, 200, 200},
  {BUZZER_TONE_CLICK / 2, 400, 0}
}, 3};

void playSound(SOUNDS sound) {
  switch (sound) {
    case SND_CONNECTED: startSound(SND_CONNECTED_OBJ); break;
    case SND_NO_WIFI: startSound(SND_NO_WIFI_OBJ); break;
    case SND_HTTP_POST_FAILED: startSound(SND_HTTP_FAIL_OBJ); break;
    case SND_ADD_POINT: startSound(SND_ADD_POINT_OBJ); break;
    case SND_UNDO: startSound(SND_UNDO_OBJ); break;
    case SND_SWITCH_TEAM: startSound(SND_SWITCH_TEAM_OBJ); break;
  }
}

// ==========================
// WIFI + API
// ==========================

void ensureWiFi() {
  bool isConnected = WiFi.status() == WL_CONNECTED;

  if (isConnected) return;
  if (WIFI_NAME == "") return;

  if (isConnected && !wasConnected) {
    log("WiFi Connected");
    playSound(SND_CONNECTED);
    
    log("Enabling WiFi modem sleep");
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  }
  else if (!isConnected && wasConnected) {
    log("Lost connection");
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
  if (WiFi.status() != WL_CONNECTED) return;

  char payload[PAYLOAD_BUFFER_SIZE];
  snprintf(payload, sizeof(payload),
    "{\"deviceId\":\"%s\",\"eventType\":\"%s\"}",
    DEVICEID.c_str(), event);

  //retries once
  for (int i = 0; i < 2; i++) {
    client.setInsecure();

    HTTPClient https;
    https.setConnectTimeout(HTTPS_CONNECT_TIMEOUT);
    https.setTimeout(HTTPS_RESPONSE_TIMEOUT);
    https.begin(client, POSTEVENT_ENDPOINT);
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

    if (code >= 200 && code < 300) return;

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
}

void undo() {
  sendEvent(EVENT_UNDO);
}

void log(String s) {
  if (DEBUG) Serial.println(s);
}

// ==========================
// SETUP
// ==========================

void setup() {
  log("\n\nSetting Up...");

  if (UNDERCLOCK) setCpuFrequencyMhz(80);

  if (DEBUG) Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

  log("Setup Complete.");
}

// ==========================
// LOOP (FIXED BUTTON LOGIC)
// ==========================

void loop() {
  ensureWiFi();
  updateSound();

  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastDebounceTime = 0;

  static unsigned long pressStartTime = 0;
  static unsigned long lastPressTime = 0;

  static bool isPressing = false;
  static bool soundUndoPlayed = false;
  static bool soundSwitchPlayed = false;

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

        if (now - lastPressTime < PRESS_COOLDOWN) return;

        if (WiFi.status() != WL_CONNECTED) {
          playSound(SND_NO_WIFI);
          return;
        }

        isPressing = true;
        pressStartTime = now;
        soundUndoPlayed = false;
        soundSwitchPlayed = false;

        digitalWrite(LED_PIN, HIGH);
      }

      // RELEASE
      else {
        digitalWrite(LED_PIN, LOW);

        if (!isPressing) return;

        isPressing = false;
        lastPressTime = now;

        unsigned long duration = now - pressStartTime;

        if (duration >= SWITCH_TEAM_HOLD_THRESHOLD) {
          switchTeam();
        }
        else if (duration >= UNDO_HOLD_THRESHOLD) {
          undo();
        }
        else {
          addPoint();
        }
      }
    }
  }

  lastReading = reading;

  // HOLD FEEDBACK
  if (isPressing && stableState == LOW) {
    unsigned long duration = now - pressStartTime;

    if (duration >= SWITCH_TEAM_HOLD_THRESHOLD && !soundSwitchPlayed) {
      playSound(SND_SWITCH_TEAM);
      soundSwitchPlayed = true;
    }
    else if (duration >= UNDO_HOLD_THRESHOLD && !soundUndoPlayed && !soundSwitchPlayed) {
      playSound(SND_UNDO);
      soundUndoPlayed = true;
    }
  }
}