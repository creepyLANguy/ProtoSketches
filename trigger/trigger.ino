const int LED_PIN = 2;
const int BUTTON_PIN = 3;
const int BUZZER_PIN = 5;

const int BUZZER_TONE_CLICK = 3000;

const int BUZZER_DURATION = 200;

const int TIMEOUT = 800;

void setup() 
{
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode (BUZZER_PIN, OUTPUT);
}


void loop() 
{
  bool pressed = digitalRead(BUTTON_PIN) == HIGH ? false : true;

  if (pressed) 
  {
    digitalWrite(LED_PIN, HIGH);
    tone(BUZZER_PIN, BUZZER_TONE_CLICK, BUZZER_DURATION);
    delay(TIMEOUT);
  }
  else 
  {    
    digitalWrite(LED_PIN, LOW);
  }
}