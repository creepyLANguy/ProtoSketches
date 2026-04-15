const int trigPin = 5;
const int echoPin = 21;
const int ledPin = 8;

// Sound speed in cm per microsecond
#define SOUND_SPEED 0.034

long duration;
float distanceCm;

void setup() {
  Serial.begin(115200);

  Serial.println("Setup Begin");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);

  digitalWrite(trigPin, LOW); // Ensure clean start

  
  Serial.println("Setup Complete");
}

void loop() {

  // Send trigger pulse
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);

  // Read echo with timeout (prevents lockups)
  duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout

  // If no echo received, treat as "no object"
  if (duration == 0) {
    Serial.println("No reading");
    digitalWrite(ledPin, LOW);
  } else {
    // Calculate distance
    distanceCm = (duration * SOUND_SPEED) / 2;

    Serial.print("Distance (cm): ");
    Serial.println(distanceCm);

    // Simple ON/OFF logic
    if (distanceCm > 10.0) {
      digitalWrite(ledPin, HIGH);
    } else {
      digitalWrite(ledPin, LOW);
    }
  }

  delay(1000); // Small delay for stability
}