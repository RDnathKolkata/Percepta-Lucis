#include <ESP32Servo.h>
#include <ESP32PWM.h>

Servo myServo;

const int servo1 = 23;
const int trigPin = 13, echoPin = 12, foreward = 32, backward = 35, pwmPin = 18;
const int trigPin2 = 14, echoPin2 = 27, foreward2 = 33, backward2 = 24, pwmPin2 = 19;
const int trigPin3 = 26, echoPin3 = 25, foreward3 = 15, backward3 = 4, pwmPin3 = 5;
int radius = 20, radius2 = 20, radius3 = 20;

int pwm = 0, pwm2 = 0, pwm3 = 0;
unsigned long servoMillis = 0;
const long servoInterval = 10;
unsigned long sensorMillis = 0;
const long sensorInterval = 100;
unsigned long printMillis = 0;
const long printInterval = 100;

int servoPos = 0;
bool increasing = true;
bool vibrationActive = false;
bool fallAlertsEnabled = true;

long readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return -1;
  return duration / 29 / 2;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 16, 17);
  myServo.attach(servo1);

  pinMode(trigPin, OUTPUT); pinMode(echoPin, INPUT);
  pinMode(foreward, OUTPUT); pinMode(backward, OUTPUT); pinMode(pwmPin, OUTPUT);
  pinMode(trigPin2, OUTPUT); pinMode(echoPin2, INPUT);
  pinMode(foreward2, OUTPUT); pinMode(backward2, OUTPUT); pinMode(pwmPin2, OUTPUT);
  pinMode(trigPin3, OUTPUT); pinMode(echoPin3, INPUT);
  pinMode(backward3, OUTPUT); pinMode(foreward3, OUTPUT); pinMode(pwmPin3, OUTPUT);
}

void loop() {
  unsigned long currentMillis = millis();

  // Receive and act on serial commands
  if (Serial1.available()) {
    String cmd = Serial1.readStringUntil('\n');
    cmd.trim();
    if (cmd == "CMD_TIME") {
      Serial.print("Time: "); Serial.println(millis() / 1000);
    }
    else if (cmd == "CMD_VIB_TOGGLE") {
        vibrationActive = !vibrationActive;
        Serial.print("Vibration motor: ");
        Serial.println(vibrationActive ? "ON" : "OFF");
        // You can add logic to turn motors ON/OFF based on vibrationActive
    }
    else if (cmd == "CMD_RANGE_UP") {
      radius += 10; if (radius > 500) radius = 500;
      Serial.print("Range increased to: "); Serial.println(radius);
    }
    else if (cmd == "CMD_RANGE_DOWN") {
      radius -=10; if (radius < 10) radius = 10;
      Serial.print("Range decreased to: "); Serial.println(radius);
    }
    else if (cmd == "CMD_FALL_TOGGLE") {
      fallAlertsEnabled = !fallAlertsEnabled;
      Serial.print("Fall alerts "); Serial.println(fallAlertsEnabled ? "ENABLED" : "DISABLED");
    }
  }

  // Servo sweep logic
  if (currentMillis - servoMillis >= servoInterval) {
    servoMillis = currentMillis;
    if (increasing) {
      servoPos++; if (servoPos >= 180) increasing = false;
    } else {
      servoPos--; if (servoPos <= 0) increasing = true;
    }
    myServo.write(servoPos);
  }

  // Sensor and motor control logic
  if (currentMillis - sensorMillis >= sensorInterval) {
    sensorMillis = currentMillis;

    long distance1 = readDistance(trigPin, echoPin);
    long distance2 = readDistance(trigPin2, echoPin2);
    long distance3 = readDistance(trigPin3, echoPin3);

    // Motor control for Sensor 1
    if (distance1 > radius || distance1 < 0) {
      pwm = 0;
      digitalWrite(foreward, LOW);
      digitalWrite(backward, LOW);
      analogWrite(pwmPin, 0);
    } else {
      pwm = map(distance1, 1, radius, 255, 5);
      if (servoPos > 90) {
        digitalWrite(foreward, HIGH); digitalWrite(backward, LOW);
        analogWrite(pwmPin, pwm);
      } else {
        digitalWrite(backward, HIGH); digitalWrite(foreward, LOW);
        analogWrite(pwmPin, pwm);
      }
    }
    //... repeat similar for Sensor 2 and 3 (same logic as before) ...

    // Print data for debugging
    if (currentMillis - printMillis >= printInterval) {
      printMillis = currentMillis;
      Serial.print("Distance 1: "); Serial.print(distance1); Serial.print(" , PWM: "); Serial.println(pwm);
      Serial.print("Distance 2: "); Serial.print(distance2); Serial.print(" , PWM2: "); Serial.println(pwm2);
      Serial.print("Distance 3: "); Serial.print(distance3); Serial.print(" , PWM3: "); Serial.println(pwm3);
      Serial.print("Servo: "); Serial.println(servoPos);
    }
  }
}
