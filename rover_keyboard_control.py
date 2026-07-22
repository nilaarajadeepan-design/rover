/*
  =====================================================================
  ROVER WIFI REMOTE CONTROL - ESP32 + OSOYOO Model Y Motor Driver
  =====================================================================

  WHAT THIS CODE DOES:
  1. Connects your ESP32 to your home WiFi network ("Bluesky")
  2. Starts a tiny "server" that listens for commands over WiFi
  3. When a command arrives (like "forward" or "turn left"), it spins
     the correct wheels in the correct direction
  4. Tracks total distance traveled using the two back-wheel encoders,
     and sends that distance back to your laptop over WiFi

  HOW THE MOTOR DRIVER WORKS (from the OSOYOO Model Y datasheet):
  - The board has 2 "channels": Channel A and Channel B
  - Each channel controls 2 wheels *together* using one set of pins:
      - IN1/IN2 pins  -> which direction wheel pair #1 on that channel spins
      - IN3/IN4 pins  -> which direction wheel pair #2 on that channel spins
      - ENA pin (PWM) -> how FAST wheel pair #1 spins (0-255)
      - ENB pin (PWM) -> how FAST wheel pair #2 spins (0-255)

  YOUR SPECIFIC WIRING:
      FRONT LEFT wheel  (socket BK4) -> Channel B, IN3/IN4 + ENB
      FRONT RIGHT wheel (socket BK2) -> Channel B, IN1/IN2 + ENA
      BACK LEFT wheel   (socket AK4) -> Channel A, IN3/IN4 + ENB
      BACK RIGHT wheel  (socket AK2) -> Channel A, IN1/IN2 + ENA
  =====================================================================
*/

#include <WiFi.h>

const char* WIFI_SSID     = "Bluesky";
const char* WIFI_PASSWORD = "Internet123$";

const int SERVER_PORT = 8888;
WiFiServer server(SERVER_PORT);
WiFiClient client;

// ---- FRONT LEFT wheel (plugged into socket BK4) ----
#define FL_IN3   25
#define FL_IN4   33
#define FL_PWM   13

// ---- FRONT RIGHT wheel (plugged into socket BK2) ----
#define FR_IN1   27
#define FR_IN2   26
#define FR_PWM   32

// ---- BACK LEFT wheel (plugged into socket AK4) ----
#define BL_IN3   18
#define BL_IN4   5
#define BL_PWM   22

// ---- BACK RIGHT wheel (plugged into socket AK2) ----
#define BR_IN1   21
#define BR_IN2   19
#define BR_PWM   23

// ============================= ENCODER CONFIGURATION =============================
#define ENCODER_BL 34
#define ENCODER_BR 35

const float PULSES_PER_REV_BL = 615.0;
const float PULSES_PER_REV_BR = 623.1;

const float WHEEL_DIAMETER_MM = 70.0;
const float WHEEL_CIRCUMFERENCE_MM = WHEEL_DIAMETER_MM * PI;

const float MM_PER_PULSE_BL = WHEEL_CIRCUMFERENCE_MM / PULSES_PER_REV_BL;
const float MM_PER_PULSE_BR = WHEEL_CIRCUMFERENCE_MM / PULSES_PER_REV_BR;

volatile unsigned long pulseCountBL = 0;
volatile unsigned long pulseCountBR = 0;

unsigned long lastPulseCountBL = 0;
unsigned long lastPulseCountBR = 0;

int directionBL = 0;
int directionBR = 0;

float totalDistanceMM = 0.0;

unsigned long lastDistanceSendTime = 0;
const unsigned long DISTANCE_SEND_INTERVAL_MS = 200;

void IRAM_ATTR onPulseBL() { pulseCountBL++; }
void IRAM_ATTR onPulseBR() { pulseCountBR++; }

const int SPEED_SLOW   = 90;
const int SPEED_MEDIUM = 150;
const int SPEED_FAST   = 255;
int DRIVE_SPEED = SPEED_MEDIUM;

const unsigned long COMMAND_TIMEOUT_MS = 500;
unsigned long lastCommandTime = 0;

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
  directionBL = (speed == 0) ? 0 : (forward ? 1 : -1);
}

void backRight(int speed, bool forward) {
  digitalWrite(BR_IN1, forward ? HIGH : LOW);
  digitalWrite(BR_IN2, forward ? LOW  : HIGH);
  analogWrite(BR_PWM, speed);
  directionBR = (speed == 0) ? 0 : (forward ? 1 : -1);
}

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

void turnLeft() {
  frontLeft(DRIVE_SPEED, false);
  backLeft(DRIVE_SPEED, false);
  frontRight(DRIVE_SPEED, true);
  backRight(DRIVE_SPEED, true);
}

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
  directionBL = 0;
  directionBR = 0;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(FL_IN3, OUTPUT); pinMode(FL_IN4, OUTPUT); pinMode(FL_PWM, OUTPUT);
  pinMode(FR_IN1, OUTPUT); pinMode(FR_IN2, OUTPUT); pinMode(FR_PWM, OUTPUT);
  pinMode(BL_IN3, OUTPUT); pinMode(BL_IN4, OUTPUT); pinMode(BL_PWM, OUTPUT);
  pinMode(BR_IN1, OUTPUT); pinMode(BR_IN2, OUTPUT); pinMode(BR_PWM, OUTPUT);

  stopAllMotors();

  pinMode(ENCODER_BL, INPUT);
  pinMode(ENCODER_BR, INPUT);
  attachInterrupt(digitalPinToInterrupt(ENCODER_BL), onPulseBL, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_BR), onPulseBR, RISING);

  Serial.print("Connecting to WiFi network: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("ROVER IP ADDRESS: ");
  Serial.println(WiFi.localIP());
  Serial.println("Type this IP into your Python script on the laptop.");

  server.begin();
  Serial.println("Server started. Waiting for laptop to connect...");
}

void loop() {

  if (!client || !client.connected()) {
    client = server.available();
    if (client) {
      Serial.println("Laptop connected!");
      lastCommandTime = millis();
    }
  }

  if (client && client.connected() && client.available()) {
    char command = client.read();
    lastCommandTime = millis();

    switch (command) {
      case 'F':
        moveForward();
        break;
      case 'B':
        moveBackward();
        break;
      case 'L':
        turnLeft();
        break;
      case 'R':
        turnRight();
        break;
      case 'S':
        stopAllMotors();
        break;
      case '1':
        DRIVE_SPEED = SPEED_SLOW;
        Serial.println("Speed set to SLOW");
        break;
      case '2':
        DRIVE_SPEED = SPEED_MEDIUM;
        Serial.println("Speed set to MEDIUM");
        break;
      case '3':
        DRIVE_SPEED = SPEED_FAST;
        Serial.println("Speed set to FAST");
        break;
      default:
        break;
    }
  }

  if (millis() - lastCommandTime > COMMAND_TIMEOUT_MS) {
    stopAllMotors();
  }

  unsigned long currentBL = pulseCountBL;
  unsigned long newPulsesBL = currentBL - lastPulseCountBL;
  lastPulseCountBL = currentBL;
  float distanceThisStepBL = newPulsesBL * MM_PER_PULSE_BL * directionBL;

  unsigned long currentBR = pulseCountBR;
  unsigned long newPulsesBR = currentBR - lastPulseCountBR;
  lastPulseCountBR = currentBR;
  float distanceThisStepBR = newPulsesBR * MM_PER_PULSE_BR * directionBR;

  totalDistanceMM += (distanceThisStepBL + distanceThisStepBR) / 2.0;

  // ---- TEMPORARY DEBUG OUTPUT ----
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

  if (client && client.connected() &&
      millis() - lastDistanceSendTime > DISTANCE_SEND_INTERVAL_MS) {
    lastDistanceSendTime = millis();
    client.print("DIST:");
    client.println(totalDistanceMM, 1);
  }
}
