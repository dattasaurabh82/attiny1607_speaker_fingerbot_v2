/*
  Simple test sketch to observe speaker LED behavior.
  Target: ATtiny1607
  Hardware:
    PC2 (pin 19) - Trigger input (comparator OUT)
    PB2 (pin 14) - TX to serial adapter RX
  Serial: 115200 baud (TX only)
  Format: [elapsed_ms] STATE duration_ms
*/

#define TRIGGER_PIN PIN_PC2
#define POLL_INTERVAL 20

bool lastState = false;
unsigned long stateStartTime = 0;
unsigned long runStartTime = 0;

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(TRIGGER_PIN, INPUT);

  runStartTime = millis();
  stateStartTime = millis();
  lastState = (PORTC.IN & PIN2_bm);

  Serial.println(F("LED Analyzer started"));
  Serial.print(F("Initial: "));
  Serial.println(lastState ? "OFF" : "ON");
}

void loop() {
  bool currentState = (PORTC.IN & PIN2_bm);

  if (currentState != lastState) {
    unsigned long duration = millis() - stateStartTime;
    unsigned long elapsed = millis() - runStartTime;

    Serial.print(F("["));
    Serial.print(elapsed);
    Serial.print(F("] "));
    Serial.print(currentState ? "OFF " : "ON");
    Serial.print(F(" (was "));
    Serial.print(lastState ? "OFF " : "ON");
    Serial.print(duration);
    Serial.println(F("ms)"));

    lastState = currentState;
    stateStartTime = millis();
  }

  delay(POLL_INTERVAL);
}