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

const int DISTANCE_THRESHOLD_CM = 10;
const int DISTANCE_HYSTERESIS_CM = 10;
const unsigned long DISTANCE_SAMPLE_INTERVAL_MS = 300;
const int ECHO_TIMEOUT = 30000;

const int TRIG_PIN = 5;
const int ECHO_PIN = 21;
const int LED_PIN = 0;
const int BUZZER_PIN = 4;

const int TEAM_A_PIN = 1;
const int TEAM_B_PIN = 2;

#define SOUND_SPEED 0.034

long detection_duration;
float distance_cm;

Preferences preferences;
DNSServer dnsServer;
WebServer server(80);

const int MAX_WIFI_NETWORKS = 5;
WiFiCreds savedWiFi[MAX_WIFI_NETWORKS];
int wifiCount = 0;

bool isConfigMode = false;
unsigned long lastStatusFlash = 0;
bool statusLedState = false;

String DEVICEID = "";

enum SOUNDS {
  SND_CONNECTED,
  SND_NO_WIFI,
  SND_HTTP_POST_FAILED,
  SND_ADD_POINT
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

const int BUZZER_TONE = 3000;
const int BUZZER_DURATION = 200;

Sound currentSound;
int soundIndex = 0;
unsigned long soundStart = 0;
bool isPlayingSound = false;

void startSound(Sound &sound) {
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
// 🔊 SOUND DEFINITIONS
// ==========================

Sound SND_ADD_POINT_OBJ = {{{BUZZER_TONE, BUZZER_DURATION, }}, 1};

Sound SND_CONNECTED_OBJ = {{{BUZZER_TONE / 4, 80, 50},
                            {BUZZER_TONE / 3, 100, 50},
                            {BUZZER_TONE / 2, 120, 0}},
                           3};

Sound SND_NO_WIFI_OBJ = {{{BUZZER_TONE / 2, 80, 50},
                          {BUZZER_TONE / 3, 80, 50},
                          {BUZZER_TONE / 4, 80, 50},
                          {BUZZER_TONE / 2, 80, 50},
                          {BUZZER_TONE / 3, 80, 50},
                          {BUZZER_TONE / 4, 80, 0}},
                         6};

Sound SND_HTTP_FAIL_OBJ = {{{BUZZER_TONE / 2, 200, 200},
                            {BUZZER_TONE / 2, 200, 200},
                            {BUZZER_TONE / 2, 400, 0}},
                           3};

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
  }
}

float measureDistance_cm(){
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  detection_duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT);

  if (detection_duration == 0) {
    return -1;
  } 
  
  return (detection_duration * SOUND_SPEED) / 2;
}

String getSelectedTeam() {
  return String(digitalRead(TEAM_A_PIN) == LOW ? 'A' : 'B');
}

void log(String s) {
  if (DEBUG == false)
    return;

  Serial.println(s);
}

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

void setup() {
  if (DEBUG)
    Serial.begin(115200);

  log("\n\nSetting Up...");

  if (UNDERCLOCK)
    setCpuFrequencyMhz(80);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TEAM_A_PIN, INPUT_PULLUP);
  pinMode(TEAM_B_PIN, INPUT_PULLUP);

  digitalWrite(TRIG_PIN, LOW);
  
  log("Setup Complete");
}

void loop() {

  updateSound();

  static unsigned long lastDistanceSample = 0;
  static bool hasTriggered = false;
  unsigned long now = millis();

  if (now - lastDistanceSample >= DISTANCE_SAMPLE_INTERVAL_MS) {
    lastDistanceSample = now;

    distance_cm = measureDistance_cm();
    if (distance_cm < 0) {
      log("No reading...");
    }
    else {
      log("Distance: " + String((int)distance_cm) + " cm");
    }

    bool objectDetected = (distance_cm > 0 && distance_cm <= DISTANCE_THRESHOLD_CM);
    bool objectCleared = (distance_cm < 0 || distance_cm > DISTANCE_THRESHOLD_CM + DISTANCE_HYSTERESIS_CM);

    if (objectDetected && !hasTriggered) {
      hasTriggered = true;
      digitalWrite(LED_PIN, HIGH);
      playSound(SND_ADD_POINT);
      log("Team selection: " + getSelectedTeam());
    } 
    else if (objectCleared) {
      hasTriggered = false;
      digitalWrite(LED_PIN, LOW);
    }
  }
}