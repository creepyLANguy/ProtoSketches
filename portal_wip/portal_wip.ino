const int LED_PIN = 3;
const int BUZZER_PIN = 4;

// ==========================
// SETUP
// ==========================

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
}

// ==========================
// LOOP
// ==========================

void loop() {
  tone(BUZZER_PIN, 300, 50);
  digitalWrite(LED_PIN, digitalRead(LED_PIN) == HIGH ? LOW : HIGH);
  delay(5000);
}