/*
  =====================================================================
  MPU-6050 HEADING TEST - just the left/right angle, no graphics
  =====================================================================

  WHAT THIS CODE DOES:
  Since your rover only ever spins flat on the floor (never tips
  forward/back or side to side), the only number that actually matters
  is HEADING - which way it's currently facing, compared to where it
  started. This sketch does just that, and prints it straight to
  Serial Monitor - no WiFi, no Python needed for this test.

  WHY THE GYROSCOPE (not the accelerometer this time):
  The accelerometer can only sense gravity's direction - and gravity
  points straight down no matter which way you're facing while lying
  flat. So it's physically incapable of measuring "which way am I
  facing" on a flat surface. The gyroscope instead measures ROTATION
  SPEED, which we add up over time to get total heading - the same
  method already built into your rover code.

  HOW TO USE THIS:
  1. Upload this sketch
  2. Open Serial Monitor (115200 baud)
  3. Watch the heading number - it should read close to 0 at rest
  4. Rotate the breadboard flat on a table, a known amount (like a
     careful 90 degree turn), and check how close the number gets
  5. Type "0" into the Serial Monitor's send box (top) and hit Enter/
     Send at any time to reset heading back to 0
  =====================================================================
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#define MPU_SDA_PIN 21
#define MPU_SCL_PIN 22

Adafruit_MPU6050 mpu;
bool mpuFound = false;

// Our running total heading, in degrees. 0 = whichever way it was
// facing at the last reset (either power-on, or typing "0").
float headingDegrees = 0.0;

// Tracks time between gyroscope readings, so we can do
// "rotation speed x time = how much it actually rotated".
unsigned long lastReadTime = 0;

unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL_MS = 200; // print 5 times per second


void setup() {
  Serial.begin(115200);
  delay(500);

  // Pull-up resistors + safety timeout, same fixes we needed before.
  pinMode(MPU_SDA_PIN, INPUT_PULLUP);
  pinMode(MPU_SCL_PIN, INPUT_PULLUP);
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(50000);
  Wire.setTimeOut(1000);

  if (mpu.begin()) {
    mpuFound = true;
    Serial.println("MPU-6050 found! Tracking heading...");
    Serial.println("Type '0' and press Enter/Send anytime to reset heading to 0.");
  } else {
    mpuFound = false;
    Serial.println("MPU-6050 NOT found - check wiring!");
  }

  lastReadTime = millis();
}


void loop() {
  if (!mpuFound) {
    delay(500);
    return;
  }

  // ---- Check if the person typed "0" into Serial Monitor to reset ----
  if (Serial.available()) {
    char incoming = Serial.read();
    if (incoming == '0') {
      headingDegrees = 0.0;
      Serial.println(">>> Heading manually reset to 0 <<<");
    }
  }

  // ---- Read the gyroscope and update our heading total ----
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  unsigned long now = millis();
  float deltaSeconds = (now - lastReadTime) / 1000.0;
  lastReadTime = now;

  // gyro.gyro.z = rotation speed around the vertical axis (radians
  // per second) - exactly the "spinning left/right while flat"
  // rotation your rover cares about.
  float rotationThisStep = gyro.gyro.z * deltaSeconds;
  headingDegrees += rotationThisStep * (180.0 / PI);

  // ---- Print it, a few times per second ----
  if (now - lastPrintTime > PRINT_INTERVAL_MS) {
    lastPrintTime = now;
    Serial.print("Heading: ");
    Serial.print(headingDegrees, 1);
    Serial.println(" degrees");
  }
}
