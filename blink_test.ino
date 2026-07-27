/*
  BLINK TEST - just checks that this new ESP32 board itself works,
  with no sensors, no WiFi, nothing else involved.
*/

#define LED_PIN 2   // Most ESP32 dev boards have a built-in LED on pin 2

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Blink test starting...");
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED ON");
  delay(500);

  digitalWrite(LED_PIN, LOW);
  Serial.println("LED OFF");
  delay(500);
}
