/*
  =====================================================================
  ROVER WIFI REMOTE CONTROL - ESP32 + OSOYOO Model Y Motor Driver, All keys working tested and verified. Code for both wheel encoders.
  =====================================================================

  WHAT THIS CODE DOES:
  1. Connects your ESP32 to your home WiFi network ("Bluesky")
  2. Starts a tiny "server" that listens for commands over WiFi
  3. When a command arrives (like "forward" or "turn left"), it spins
     the correct wheels in the correct direction

  HOW THE MOTOR DRIVER WORKS (from the OSOYOO Model Y datasheet):
  - The board has 2 "channels": Channel A and Channel B
  - Each channel controls 2 wheels *together* using one set of pins:
      - IN1/IN2 pins  -> which direction wheel pair #1 on that channel spins
      - IN3/IN4 pins  -> which direction wheel pair #2 on that channel spins
      - ENA pin (PWM) -> how FAST wheel pair #1 spins (0-255)
      - ENB pin (PWM) -> how FAST wheel pair #2 spins (0-255)

  YOUR SPECIFIC WIRING (matched to your rover - double check against
  your real wires if anything doesn't move right!):

      FRONT LEFT wheel  (socket BK4) -> Channel B, IN3/IN4 + ENB
      FRONT RIGHT wheel (socket BK2) -> Channel B, IN1/IN2 + ENA
      BACK LEFT wheel   (socket AK4) -> Channel A, IN3/IN4 + ENB
      BACK RIGHT wheel  (socket AK2) -> Channel A, IN1/IN2 + ENA

  You will need the "WiFi.h" library, which comes built into the
  ESP32 Arduino core (no extra install needed).

  BEFORE YOU UPLOAD:
  1. Fill in your WiFi password below (WIFI_PASSWORD)
  2. Plug in the ESP32 with USB, select the right board + port in
     Arduino IDE, and click Upload
  3. Open the Serial Monitor (baud rate 115200) - once it connects
     to WiFi, it will print an IP ADDRESS. WRITE THAT DOWN. You will
     type that IP address into the Python script on your laptop.
  4. After that first upload, you can unplug USB and run the rover
     purely on battery power - it will auto-connect to WiFi every
     time it powers on, since the WiFi info is now saved in its code.
  =====================================================================
*/

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ============================= WIFI SETTINGS =============================
// Your home WiFi network name and password.
const char* WIFI_SSID     = "Bluesky";
const char* WIFI_PASSWORD = "Internet123$"; // <-- fill this in!

// The "port" is just a number both the ESP32 and laptop agree to talk on.
// 8888 is a safe, uncommon choice - no need to change it.
const int SERVER_PORT = 8888;

// This creates the actual WiFi server object that will listen for
// your laptop trying to connect.
WiFiServer server(SERVER_PORT);

// This will hold the connection to your laptop once it connects.
WiFiClient client;


// ============================= PIN CONFIGURATION =============================
// !!! IMPORTANT !!!
// These pins are wired to match YOUR rover, based on what you told me.
// If a wheel spins the wrong way or doesn't move, check these against
// your actual wires first - this is the #1 place bugs hide.

// ---- FRONT LEFT wheel (plugged into socket BK4) ----
#define FL_IN3   25   // direction pin
#define FL_IN4   33   // direction pin
#define FL_PWM   13   // speed pin (ENB)

// ---- FRONT RIGHT wheel (plugged into socket BK2) ----
#define FR_IN1   27   // direction pin
#define FR_IN2   26   // direction pin
#define FR_PWM   32   // speed pin (ENA)

// ---- BACK LEFT wheel (plugged into socket AK4) ----
#define BL_IN3   18   // direction pin
#define BL_IN4   5    // direction pin
#define BL_PWM   22   // speed pin (ENB)

// ---- BACK RIGHT wheel (plugged into socket AK2) ----
#define BR_IN1   21   // direction pin
#define BR_IN2   19   // direction pin
#define BR_PWM   23   // speed pin (ENA)


// ============================= ENCODER CONFIGURATION =============================
// Only the two BACK motors have encoders built in (the front motors on
// this kit don't). These let us measure exactly how far the rover has
// actually traveled, using the wheel's own rotation as a ruler.

#define ENCODER_BL 34   // Back Left encoder signal wire (Yellow)
#define ENCODER_BR 35   // Back Right encoder signal wire (Yellow)

// ---- YOUR REAL CALIBRATION NUMBERS ----
// These came from spinning each wheel by hand exactly 10 full turns
// and counting how many pulses the encoder produced. Every real motor
// is a little different, which is why we measured YOUR actual rover
// instead of trusting a generic spec sheet number.
const float PULSES_PER_REV_BL = 615.0;   // measured: 6150 pulses / 10 turns
const float PULSES_PER_REV_BR = 623.1;   // measured: 6231 pulses / 10 turns

// Your wheel diameter, measured with a ruler: 70mm (7cm).
// Circumference = diameter x PI = how far the rover moves in ONE
// full wheel rotation. PI is a built-in Arduino constant (3.14159...).
const float WHEEL_DIAMETER_MM = 70.0;
const float WHEEL_CIRCUMFERENCE_MM = WHEEL_DIAMETER_MM * PI;

// This is the actual conversion factor we'll use: "how many
// millimeters does the rover travel for every single encoder pulse?"
const float MM_PER_PULSE_BL = WHEEL_CIRCUMFERENCE_MM / PULSES_PER_REV_BL;
const float MM_PER_PULSE_BR = WHEEL_CIRCUMFERENCE_MM / PULSES_PER_REV_BR;

// "volatile" tells the compiler these numbers can change unexpectedly,
// inside an interrupt, at any moment - this keeps the counting accurate.
volatile unsigned long pulseCountBL = 0;
volatile unsigned long pulseCountBR = 0;

// These remember the pulse count from the LAST time we checked, so we
// can figure out "how many NEW pulses happened since last time".
unsigned long lastPulseCountBL = 0;
unsigned long lastPulseCountBR = 0;

// Which direction each back wheel is CURRENTLY commanded to spin:
// +1 = forward, -1 = backward, 0 = stopped. We need this because the
// encoder itself can't tell direction (we're only using one signal
// wire per wheel) - so we track direction ourselves, from whichever
// arrow key command is currently active.
int directionBL = 0;
int directionBR = 0;

// Our running total: how far the rover has traveled overall, in
// millimeters, since it was turned on. Like a car's odometer - it
// only ever goes UP, whether driving forward or backward, and does
// NOT account for turns (that's what the upcoming IMU sensor is for).
// We track EACH wheel's own cumulative distance separately, then
// combine them only when reporting - this avoids a subtle bug where
// comparing tiny instant-by-instant pulse timing between two wheels
// caused inflated readings (explained more below, near the averaging code).
float totalDistanceMM_BL = 0.0;
float totalDistanceMM_BR = 0.0;

// Used to send distance updates back to the laptop periodically,
// instead of flooding the connection with updates constantly.
unsigned long lastDistanceSendTime = 0;
const unsigned long DISTANCE_SEND_INTERVAL_MS = 200; // 5 times per second


// =====================================================================
//  ENCODER INTERRUPT FUNCTIONS ("ISRs")
//  These run AUTOMATICALLY, instantly, every time their pin's encoder
//  signal pulses - completely separate from the rest of the program,
//  so we never miss a pulse even during a WiFi command or motor change.
// =====================================================================
void IRAM_ATTR onPulseBL() { pulseCountBL++; }
void IRAM_ATTR onPulseBR() { pulseCountBR++; }


// ============================= MPU-6050 (DIRECTION SENSOR) CONFIGURATION =============================
// This chip has both an accelerometer (tilt) and gyroscope (spin
// speed). We only need the gyroscope's Z-axis here - that's the one
// that measures spinning left/right while sitting flat on the floor,
// which is exactly "which way is the rover facing."

// Different pins than the motor driver/encoders, since I2C needs its
// own dedicated SDA (data) and SCL (clock) lines.
#define MPU_SDA_PIN 4
#define MPU_SCL_PIN 14

Adafruit_MPU6050 mpu;   // this object represents our sensor
bool mpuFound = false;  // tracks whether the sensor was detected at startup

// Our best current guess of which way the rover is facing, in degrees.
// 0 = whichever direction it was facing when it was turned on.
float headingDegrees = 0.0;

// We need to know how much TIME has passed between gyroscope
// readings, since "rotation speed" x "time" = "how far it rotated".
unsigned long lastGyroReadTime = 0;

// Used to send heading updates back to the laptop periodically,
// same pattern as the distance updates.
unsigned long lastHeadingSendTime = 0;
const unsigned long HEADING_SEND_INTERVAL_MS = 200; // 5 times per second


// ============================= DRIVING SETTINGS =============================
// How fast the wheels spin, from 0 (stopped) to 255 (full speed).
// You can change speed anytime from your laptop by pressing 1, 2, or 3 -
// it takes effect immediately, even while the rover is moving.
const int SPEED_SLOW   = 90;    // gentle - good for tight spaces
const int SPEED_MEDIUM = 150;   // normal driving speed
const int SPEED_FAST   = 255;   // full power

// This is NOT "const" because it changes while the program runs,
// whenever a speed command comes in. It starts at MEDIUM.
int DRIVE_SPEED = SPEED_MEDIUM;

// SAFETY FEATURE: if the ESP32 stops hearing from your laptop for this
// many milliseconds (for example, WiFi drops, or you close the Python
// script), it automatically stops the motors. This stops the rover
// from driving off on its own if the connection is lost.
const unsigned long COMMAND_TIMEOUT_MS = 500;
unsigned long lastCommandTime = 0;


// =====================================================================
//  LOW-LEVEL MOTOR FUNCTIONS
//  Each function controls ONE wheel. "speed" is 0-255.
//  We follow OSOYOO's convention: first IN pin HIGH + second IN pin LOW
//  means "forward". If a wheel spins backward when it should go forward,
//  just swap which pin gets HIGH and which gets LOW for THAT wheel only.
// =====================================================================

void frontLeft(int speed, bool forward) {
  digitalWrite(FL_IN3, forward ? HIGH : LOW);
  digitalWrite(FL_IN4, forward ? LOW  : HIGH);
  analogWrite(FL_PWM, speed);
}

void frontRight(int speed, bool forward) {
  digitalWrite(FR_IN1, forward ? HIGH : LOW);
  digitalWrite(FR_IN2, forward ? LOW  : HIGH);
  analogWrite(FR_PWM, speed);
}

void backLeft(int speed, bool forward) {
  digitalWrite(BL_IN3, forward ? HIGH : LOW);
  digitalWrite(BL_IN4, forward ? LOW  : HIGH);
  analogWrite(BL_PWM, speed);

  // Remember which way this wheel is currently spinning, so our
  // distance math later knows whether to add or subtract.
  directionBL = (speed == 0) ? 0 : (forward ? 1 : -1);
}

void backRight(int speed, bool forward) {
  digitalWrite(BR_IN1, forward ? HIGH : LOW);
  digitalWrite(BR_IN2, forward ? LOW  : HIGH);
  analogWrite(BR_PWM, speed);

  // Remember which way this wheel is currently spinning, so our
  // distance math later knows whether to add or subtract.
  directionBR = (speed == 0) ? 0 : (forward ? 1 : -1);
}


// =====================================================================
//  HIGH-LEVEL MOVEMENT FUNCTIONS
//  These are the "remote control" actions - forward, backward,
//  turn left, turn right, and stop. This is simple tank-style turning
//  (pivoting), which is the easiest to start with. Since your wheels
//  are mecanum wheels, sideways strafing is also possible later - just
//  not part of this first mini project.
// =====================================================================

void moveForward() {
  frontLeft(DRIVE_SPEED, true);
  frontRight(DRIVE_SPEED, true);
  backLeft(DRIVE_SPEED, true);
  backRight(DRIVE_SPEED, true);
}

void moveBackward() {
  frontLeft(DRIVE_SPEED, false);
  frontRight(DRIVE_SPEED, false);
  backLeft(DRIVE_SPEED, false);
  backRight(DRIVE_SPEED, false);
}

// Pivot turn left: left wheels spin backward, right wheels spin forward.
// The rover rotates in place, like a tank.
void turnLeft() {
  frontLeft(DRIVE_SPEED, false);
  backLeft(DRIVE_SPEED, false);
  frontRight(DRIVE_SPEED, true);
  backRight(DRIVE_SPEED, true);
}

// Pivot turn right: right wheels spin backward, left wheels spin forward.
void turnRight() {
  frontLeft(DRIVE_SPEED, true);
  backLeft(DRIVE_SPEED, true);
  frontRight(DRIVE_SPEED, false);
  backRight(DRIVE_SPEED, false);
}

void stopAllMotors() {
  analogWrite(FL_PWM, 0);
  analogWrite(FR_PWM, 0);
  analogWrite(BL_PWM, 0);
  analogWrite(BR_PWM, 0);

  // No wheel is moving, so distance shouldn't accumulate right now.
  directionBL = 0;
  directionBR = 0;
}


// =====================================================================
//  SETUP - runs once when the ESP32 powers on
// =====================================================================
void setup() {
  // Start Serial so we can print helpful messages (see them in
  // Arduino IDE's Serial Monitor, set to 115200 baud).
  Serial.begin(115200);
  delay(500);

  // Set every motor control pin to OUTPUT mode.
  pinMode(FL_IN3, OUTPUT); pinMode(FL_IN4, OUTPUT); pinMode(FL_PWM, OUTPUT);
  pinMode(FR_IN1, OUTPUT); pinMode(FR_IN2, OUTPUT); pinMode(FR_PWM, OUTPUT);
  pinMode(BL_IN3, OUTPUT); pinMode(BL_IN4, OUTPUT); pinMode(BL_PWM, OUTPUT);
  pinMode(BR_IN1, OUTPUT); pinMode(BR_IN2, OUTPUT); pinMode(BR_PWM, OUTPUT);

  // Make sure the rover starts completely stopped.
  stopAllMotors();

  // ---- Set up the encoders ----
  pinMode(ENCODER_BL, INPUT);
  pinMode(ENCODER_BR, INPUT);

  // Tell the ESP32: "whenever this pin goes from LOW to HIGH (RISING),
  // immediately run this function" - this catches every single pulse,
  // even fast ones, without us having to constantly check in a loop.
  attachInterrupt(digitalPinToInterrupt(ENCODER_BL), onPulseBL, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_BR), onPulseBR, RISING);

  // ---- Set up the MPU-6050 direction sensor ----
  // Enable internal "pull-up" resistors on the I2C pins. Without these,
  // the data lines can float unpredictably between messages, which can
  // cause the ESP32's I2C communication to hang - and a hung I2C bus
  // can trigger the whole chip to safety-restart (which is exactly the
  // crash-loop we were seeing).
  pinMode(MPU_SDA_PIN, INPUT_PULLUP);
  pinMode(MPU_SCL_PIN, INPUT_PULLUP);

  // Tell the ESP32 which pins to use for I2C communication (different
  // from the default pins, since those are already used by the motors).
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);

  // Slow down the I2C communication speed. Faster speeds need cleaner,
  // stronger electrical connections (like proper pull-up resistors) to
  // work reliably - slowing down often helps on breadboard wiring
  // without those, at the cost of the sensor updating slightly less
  // often (which doesn't matter for our purposes).
  Wire.setClock(50000); // 50kHz, slower than the usual 100kHz default

  // Extra safety net: if an I2C operation doesn't finish within this
  // many milliseconds, give up on it instead of hanging forever. This
  // means even if the sensor has a wiring problem, the ESP32 itself
  // will keep running normally instead of crash-looping.
  Wire.setTimeOut(1000);

  if (mpu.begin()) {
    mpuFound = true;
    Serial.println("MPU-6050 direction sensor found!");
  } else {
    mpuFound = false;
    Serial.println("MPU-6050 NOT found - check wiring! Heading will stay at 0.");
  }
  lastGyroReadTime = millis();

  // ---- Connect to WiFi ----
  Serial.print("Connecting to WiFi network: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Keep checking every half second until connected.
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("ROVER IP ADDRESS: ");
  Serial.println(WiFi.localIP());   // <-- Write this IP down!
  Serial.println("Type this IP into your Python script on the laptop.");

  // Start listening for a connection from your laptop.
  server.begin();
  Serial.println("Server started. Waiting for laptop to connect...");
}


// =====================================================================
//  LOOP - runs over and over, forever, while the ESP32 is powered on
// =====================================================================
void loop() {

  // If nobody is connected yet, check if a new laptop connection
  // is waiting, and accept it.
  if (!client || !client.connected()) {
    client = server.available();
    if (client) {
      Serial.println("Laptop connected!");
      lastCommandTime = millis(); // reset the safety timer
    }
  }

  // If we DO have a connected laptop, check if it sent us any data.
  if (client && client.connected() && client.available()) {

    // Read one character - this is our command.
    char command = client.read();
    lastCommandTime = millis(); // we heard from the laptop, reset timer

    // Decide what to do based on the character received.
    switch (command) {
      case 'F': // Forward (Up arrow)
        moveForward();
        break;
      case 'B': // Backward (Down arrow)
        moveBackward();
        break;
      case 'L': // Turn left (Left arrow)
        turnLeft();
        break;
      case 'R': // Turn right (Right arrow)
        turnRight();
        break;
      case 'S': // Stop (no arrow key held)
        stopAllMotors();
        break;
      case '1': // Speed: SLOW
        DRIVE_SPEED = SPEED_SLOW;
        Serial.println("Speed set to SLOW");
        break;
      case '2': // Speed: MEDIUM
        DRIVE_SPEED = SPEED_MEDIUM;
        Serial.println("Speed set to MEDIUM");
        break;
      case '3': // Speed: FAST
        DRIVE_SPEED = SPEED_FAST;
        Serial.println("Speed set to FAST");
        break;
      case '0': // Manually reset distance tracking to zero
        totalDistanceMM_BL = 0.0;
        totalDistanceMM_BR = 0.0;
        pulseCountBL = 0;
        pulseCountBR = 0;
        lastPulseCountBL = 0;
        lastPulseCountBR = 0;
        Serial.println("Distance tracker manually reset to 0.");
        break;
      case '9': // Manually reset heading to zero (current facing = "0 degrees")
        headingDegrees = 0.0;
        Serial.println("Heading manually reset to 0.");
        break;
      default:
        // Unknown character - ignore it, don't do anything unexpected.
        break;
    }
  }

  // ---- SAFETY CHECK ----
  // If we haven't heard ANY command in a while (laptop closed, WiFi
  // dropped, etc.), stop the motors so the rover doesn't run away.
  if (millis() - lastCommandTime > COMMAND_TIMEOUT_MS) {
    stopAllMotors();
  }

  // ---- UPDATE DISTANCE TRAVELED ----
  // Figure out how many NEW pulses have happened since we last checked,
  // for each back wheel, then convert those pulses into millimeters
  // using our calibration numbers. We apply the wheel's CURRENT
  // direction (+1 forward, -1 backward, 0 stopped) so driving backward
  // correctly subtracts instead of adding.
  //
  // Since we have TWO back wheels measuring the same overall movement,
  // we average their two distance readings together - this also
  // naturally smooths out the small difference between them (remember,
  // your two wheels measured slightly different pulses-per-turn).

  unsigned long currentBL = pulseCountBL;   // grab a snapshot
  unsigned long newPulsesBL = currentBL - lastPulseCountBL;
  lastPulseCountBL = currentBL;
  totalDistanceMM_BL += newPulsesBL * MM_PER_PULSE_BL * directionBL;

  unsigned long currentBR = pulseCountBR;   // grab a snapshot
  unsigned long newPulsesBR = currentBR - lastPulseCountBR;
  lastPulseCountBR = currentBR;
  totalDistanceMM_BR += newPulsesBR * MM_PER_PULSE_BR * directionBR;

  // ---- COMBINE BOTH WHEELS' TOTALS (the fix) ----
  // Earlier, we tried to decide "is only one wheel active?" by
  // comparing pulse counts during each tiny loop cycle - but that was
  // buggy: two real, working wheels almost never produce a pulse in
  // the EXACT same microsecond-sized instant, so the code kept
  // wrongly concluding "only one wheel is active" and used its full,
  // un-halved value - inflating the total distance.
  //
  // The fix: track each wheel's own running total separately (above),
  // the whole time they're driving. Only HERE, when we actually need
  // one combined number, do we check "has this wheel EVER produced a
  // single pulse since power-on?" - a wheel that's truly disconnected
  // will have exactly 0 total pulses always, not just in one instant.
  float totalDistanceMM;
  if (pulseCountBL == 0 && pulseCountBR > 0) {
    totalDistanceMM = totalDistanceMM_BR;       // BL truly never connected
  } else if (pulseCountBR == 0 && pulseCountBL > 0) {
    totalDistanceMM = totalDistanceMM_BL;       // BR truly never connected
  } else {
    totalDistanceMM = (totalDistanceMM_BL + totalDistanceMM_BR) / 2.0;
  }

  // ---- TEMPORARY DEBUG OUTPUT ----
  // Prints the raw pulse counts once per second to Serial Monitor, so
  // we can check whether the encoders are actually detecting spins at
  // all - completely separate from whether the WiFi message is working.
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 1000) {
    lastDebugPrint = millis();
    Serial.print("[DEBUG] Raw pulses - BL: ");
    Serial.print(pulseCountBL);
    Serial.print("  BR: ");
    Serial.print(pulseCountBR);
    Serial.print("   Total distance (mm): ");
    Serial.println(totalDistanceMM);
  }

  // ---- SEND DISTANCE BACK TO THE LAPTOP ----
  // A few times per second, send the current total distance to
  // whichever laptop is connected, so it can show up on your screen
  // and get logged, right alongside your driving commands.
  if (client && client.connected() &&
      millis() - lastDistanceSendTime > DISTANCE_SEND_INTERVAL_MS) {
    lastDistanceSendTime = millis();

    // We send it as text starting with "DIST:" so the Python script
    // can tell this message apart from anything else, followed by
    // the distance in millimeters, then a newline character so the
    // laptop knows where the message ends.
    client.print("DIST:");
    client.println(totalDistanceMM, 1);   // 1 = show 1 decimal place

    // ALSO send the raw pulse counts and current direction, so we can
    // diagnose encoder issues from the Python log even when USB isn't
    // connected (since WiFi and USB are separate paths).
    client.print("DEBUG:BL=");
    client.print(pulseCountBL);
    client.print(",BR=");
    client.print(pulseCountBR);
    client.print(",dirBL=");
    client.print(directionBL);
    client.print(",dirBR=");
    client.println(directionBR);
  }

  // ---- UPDATE HEADING (WHICH WAY THE ROVER IS FACING) ----
  // Only do this if the sensor was actually found at startup - if it
  // wasn't wired correctly, we skip this rather than crash the program.
  if (mpuFound) {
    sensors_event_t accel, gyro, temp;
    mpu.getEvent(&accel, &gyro, &temp);

    // How much time has passed since our last reading, in seconds.
    unsigned long now = millis();
    float deltaSeconds = (now - lastGyroReadTime) / 1000.0;
    lastGyroReadTime = now;

    // gyro.gyro.z = rotation SPEED around the vertical axis, in
    // radians per second. Multiplying by time = how much it actually
    // rotated since the last check. Same idea as speed x time = distance,
    // just for spinning instead of driving straight.
    float rotationThisStep = gyro.gyro.z * deltaSeconds;

    // Convert radians to degrees (easier to think about) and add it
    // to our running total heading.
    headingDegrees += rotationThisStep * (180.0 / PI);
  }

  // ---- SEND HEADING BACK TO THE LAPTOP ----
  if (client && client.connected() &&
      millis() - lastHeadingSendTime > HEADING_SEND_INTERVAL_MS) {
    lastHeadingSendTime = millis();

    // "HEAD:" prefix so the Python script can tell this apart from
    // DIST and DEBUG messages.
    client.print("HEAD:");
    client.println(headingDegrees, 1);
  }
}
