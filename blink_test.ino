/*
  =====================================================================
  ROVER WIFI REMOTE CONTROL - ESP32 + OSOYOO Model Y Motor Driver
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

// ============================= WIFI SETTINGS =============================
// Your home WiFi network name and password.
const char* WIFI_SSID     = "Bluesky";
const char* WIFI_PASSWORD = "PUT_YOUR_WIFI_PASSWORD_HERE"; // <-- fill this in!

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
}

void backRight(int speed, bool forward) {
  digitalWrite(BR_IN1, forward ? HIGH : LOW);
  digitalWrite(BR_IN2, forward ? LOW  : HIGH);
  analogWrite(BR_PWM, speed);
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
}