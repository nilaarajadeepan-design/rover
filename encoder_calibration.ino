/*
  =====================================================================
  ENCODER CALIBRATION TOOL - figure out your rover's real PPR
  (Back wheels only - your kit's front motors don't have encoders)
  =====================================================================

  WHAT THIS CODE DOES:
  This is a TEMPORARY sketch used just once to measure exactly how
  many encoder "pulses" happen every time a wheel makes one full turn.

  HOW TO USE THIS:
  1. Wire the Yellow (signal) wire from EACH back wheel's encoder to
     the pins below. White goes to GND, Blue goes to 3.3V (same for
     both motors). Leave Green disconnected.
  2. Upload this sketch, open Serial Monitor (115200 baud)
  3. By HAND, slowly spin the BACK LEFT wheel exactly 10 full turns
     (use a piece of tape on the wheel to help you count turns)
  4. Read the number the Serial Monitor shows for "Back Left" and
     write it down
  5. Do the same for Back Right
  6. Send me both numbers - according to the motor's official
     datasheet (11 pulses per motor turn, 56:1 gear ratio), you should
     see something close to 616 pulses per full wheel turn. If your
     real numbers are way off from that, tell me - it might mean a
     wire is swapped.

  WIRING:
      Back Left  signal (Yellow) wire -> GPIO 34
      Back Right signal (Yellow) wire -> GPIO 35
      Both motors' White wire -> ESP32 GND
      Both motors' Blue wire  -> ESP32 3.3V
  =====================================================================
*/

// ============================= PIN CONFIGURATION =============================
#define ENCODER_BL 34   // Back Left encoder signal wire (Yellow)
#define ENCODER_BR 35   // Back Right encoder signal wire (Yellow)

// ============================= PULSE COUNTERS =============================
// "volatile" tells the compiler these numbers can change unexpectedly,
// inside an interrupt, at any moment - this keeps the counting accurate.
volatile unsigned long countBL = 0;
volatile unsigned long countBR = 0;

// =====================================================================
//  INTERRUPT FUNCTIONS ("ISRs")
//  These run AUTOMATICALLY, instantly, every single time their pin's
//  encoder signal changes from LOW to HIGH (one "pulse"). We just add
//  one to that wheel's counter every time this happens.
// =====================================================================
void IRAM_ATTR onPulseBL() { countBL++; }
void IRAM_ATTR onPulseBR() { countBR++; }


void setup() {
  Serial.begin(115200);
  delay(500);

  // Set each encoder pin to INPUT mode.
  pinMode(ENCODER_BL, INPUT);
  pinMode(ENCODER_BR, INPUT);

  // Tell the ESP32: "whenever this pin goes from LOW to HIGH (RISING),
  // immediately run this function" - this catches every pulse, even
  // fast ones, without constantly checking in a loop.
  attachInterrupt(digitalPinToInterrupt(ENCODER_BL), onPulseBL, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_BR), onPulseBR, RISING);

  Serial.println("Encoder calibration ready!");
  Serial.println("Spin each back wheel by hand, 10 full turns, one at a time.");
  Serial.println("Expected ballpark (from datasheet): about 616 pulses per 10 turns... wait, per turn!");
  Serial.println("So after 10 full turns, expect somewhere around 6160 total pulses.");
  Serial.println();
}

void loop() {
  // Print both counts once per second, so you can watch them rise
  // in real time as you spin each wheel by hand.
  Serial.print("Back Left: ");
  Serial.print(countBL);
  Serial.print("   Back Right: ");
  Serial.println(countBR);

  delay(1000);
}