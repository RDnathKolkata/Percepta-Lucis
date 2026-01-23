/*
 * ESP32 Fall Detection System - CLEAN VERSION
 * 
 * Hardware:
 * - ESP32 DevKit
 * - MPU6050 (I2C: SDA=GPIO21, SCL=GPIO22)
 * - Joystick Module (VRx=GPIO34, VRy=GPIO35, SW=GPIO32)
 * - SIM800L (TX=GPIO17, RX=GPIO16, RST=GPIO4)
 * 
 * Features:
 * - Fall detection using MPU6050
 * - 15-second countdown with joystick cancel
 * - Emergency SMS + voice call via SIM800L
 * - Joystick controls ultrasonic ESP32 (range up/down)
 * - Button press announces time
 */

#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <HTTPClient.h>

/* USER CONFIGURATION */

// WiFi creds
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// Emergency contact (with country code)
const char* emergencyPhone = "+918420046163";

// Server endpoints
const char* laptopAlertUrl = "http://192.168.1.100:8000/fall_alert";
const char* audioAlertUrl = "http://192.168.1.101/alert";

// Pin definitions
const int VRxPin = 34;        // Joystick X-axis
const int VRyPin = 35;        // Joystick Y-axis  
const int buttonPin = 32;     // Joystick button

// SIM800L pins
const int SIM800_TX = 17;     // ESP32 TX1 → SIM800L RX
const int SIM800_RX = 16;     // ESP32 RX1 → SIM800L TX
const int SIM800_RST = 4;     // SIM800L Reset

// UART to ESP-4 (Ultrasonic) - DIFFERENT PINS!
const int ULTRASONIC_TX = 26; // ESP32 TX2 → ESP-4 RX (GPIO16)
const int ULTRASONIC_RX = 27; // ESP32 RX2 → ESP-4 TX (GPIO17)

// Fall detection parameters
const float FALL_THRESHOLD = 2.5;
const float FREEFALL_THRESHOLD = 0.5;
const unsigned long COUNTDOWN_TIME = 15000;  // 15 seconds

// Joystick parameters
const int ADC_CENTER = 2048;
const int DEADZONE = 200;
const int UPPER_THRESHOLD = ADC_CENTER + DEADZONE;
const int LOWER_THRESHOLD = ADC_CENTER - DEADZONE;

// Time request cooldown
unsigned long lastTimeRequest = 0;
const unsigned long TIME_REQUEST_COOLDOWN = 2000;  // 2 seconds

/* ============================================
   GLOBAL VARIABLES
   ============================================ */

MPU6050 mpu;
HardwareSerial sim800(1);           // UART1 for SIM800L
HardwareSerial ultrasonicSerial(2); // UART2 for ESP-4

enum SystemState {
  STATE_MONITORING,
  STATE_COUNTDOWN,
  STATE_CALLING,
  STATE_PAUSED
};

SystemState currentState = STATE_MONITORING;
unsigned long countdownStartTime = 0;
unsigned long pauseStartTime = 0;
unsigned long lastFallCheck = 0;
bool emergencyCanceled = false;

/* ============================================
   SIM800L FUNCTIONS
   ============================================ */

void sim800_init() {
  pinMode(SIM800_RST, OUTPUT);
  digitalWrite(SIM800_RST, HIGH);
  delay(100);
  
  sim800.begin(9600, SERIAL_8N1, SIM800_RX, SIM800_TX);
  
  Serial.println("🔄 Initializing SIM800L...");
  delay(3000);
  
  // Reset module
  digitalWrite(SIM800_RST, LOW);
  delay(100);
  digitalWrite(SIM800_RST, HIGH);
  delay(3000);
  
  sim800.println("AT");
  delay(1000);
  
  while (sim800.available()) {
    String response = sim800.readString();
    if (response.indexOf("OK") >= 0) {
      Serial.println("✅ SIM800L responding");
    }
  }
  
  sim800.println("AT+CMGF=1");  // SMS text mode
  delay(1000);
  
  sim800.println("AT+CSQ");     // Signal strength
  delay(1000);
  while (sim800.available()) {
    Serial.println(sim800.readString());
  }
  
  Serial.println("✅ SIM800L initialized");
}

void sendSMS(const char* number, const char* message) {
  Serial.printf("📤 Sending SMS to %s\n", number);
  
  sim800.print("AT+CMGS=\"");
  sim800.print(number);
  sim800.println("\"");
  delay(1000);
  
  sim800.print(message);
  delay(100);
  
  sim800.write(26); // Ctrl+Z
  delay(5000);
  
  while (sim800.available()) {
    Serial.println(sim800.readString());
  }
  
  Serial.println("✅ SMS sent");
}

void makeCall(const char* number) {
  Serial.printf("📞 Calling %s\n", number);
  
  sim800.print("ATD");
  sim800.print(number);
  sim800.println(";");
  delay(1000);
  
  while (sim800.available()) {
    Serial.println(sim800.readString());
  }
  
  Serial.println("✅ Call initiated - ringing for 30 seconds");
  delay(30000);
  
  sim800.println("ATH");  // Hang up
  delay(1000);
  
  Serial.println("📵 Call ended");
}

/* ============================================
   FALL DETECTION
   ============================================ */

bool checkForFall() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  float AccX = ax / 16384.0;
  float AccY = ay / 16384.0;
  float AccZ = az / 16384.0;
  
  float AccMagnitude = sqrt(AccX * AccX + AccY * AccY + AccZ * AccZ);
  
  if (millis() - lastFallCheck > 1000) {
    Serial.printf("📊 Accel: %.2fg | State: ", AccMagnitude);
    switch(currentState) {
      case STATE_MONITORING: Serial.println("MONITORING"); break;
      case STATE_COUNTDOWN: Serial.println("COUNTDOWN"); break;
      case STATE_CALLING: Serial.println("CALLING"); break;
      case STATE_PAUSED: Serial.println("PAUSED"); break;
    }
    lastFallCheck = millis();
  }
  
  if (AccMagnitude > FALL_THRESHOLD || AccMagnitude < FREEFALL_THRESHOLD) {
    Serial.printf("⚠️ Abnormal acceleration: %.2fg\n", AccMagnitude);
    return true;
  }
  
  return false;
}

/* ============================================
   JOYSTICK FUNCTIONS
   ============================================ */

bool checkCancelButton() {
  int buttonState = digitalRead(buttonPin);
  if (buttonState == LOW) {
    delay(50);
    if (digitalRead(buttonPin) == LOW) {
      Serial.println("🔘 Button pressed!");
      return true;
    }
  }
  return false;
}

void requestTimeAnnouncement() {
  Serial.println("🕐 Time requested!");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi not connected - cannot announce time");
    return;
  }
  
  HTTPClient http;
  http.begin(audioAlertUrl);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  
  // Calculate uptime
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  minutes = minutes % 60;
  seconds = seconds % 60;
  
  String payload = "{\"object\":\"time\",\"distance\":0,\"type\":\"announce_time\",";
  payload += "\"hours\":" + String(hours % 24) + ",";
  payload += "\"minutes\":" + String(minutes) + ",";
  payload += "\"seconds\":" + String(seconds) + "}";
  
  int responseCode = http.POST(payload);
  if (responseCode > 0) {
    Serial.printf("🔊 Time request sent: %02lu:%02lu:%02lu\n", hours % 24, minutes, seconds);
  } else {
    Serial.println("⚠️ Failed to send time request");
  }
  
  http.end();
}

void handleJoystickCommands() {
  int xVal = analogRead(VRxPin);
  int yVal = analogRead(VRyPin);
  int buttonState = digitalRead(buttonPin);
  
  static unsigned long lastCommand = 0;
  
  // Direction commands (debounced)
  if (millis() - lastCommand >= 500) {
    
    // ⬆️ UP: Increase ultrasonic range
    if (yVal > UPPER_THRESHOLD) {
      ultrasonicSerial.println("CMD_RANGE_UP");
      Serial.println("📤 → Ultrasonic: RANGE_UP");
      lastCommand = millis();
    }
    // ⬇️ DOWN: Decrease ultrasonic range
    else if (yVal < LOWER_THRESHOLD) {
      ultrasonicSerial.println("CMD_RANGE_DOWN");
      Serial.println("📤 → Ultrasonic: RANGE_DOWN");
      lastCommand = millis();
    }
    
    // ⬅️ LEFT: Focus left sensor
    if (xVal < LOWER_THRESHOLD) {
      ultrasonicSerial.println("CMD_FOCUS_LEFT");
      Serial.println("📤 → Ultrasonic: FOCUS_LEFT");
      lastCommand = millis();
    }
    // ➡️ RIGHT: Focus right sensor
    else if (xVal > UPPER_THRESHOLD) {
      ultrasonicSerial.println("CMD_FOCUS_RIGHT");
      Serial.println("📤 → Ultrasonic: FOCUS_RIGHT");
      lastCommand = millis();
    }
  }
  
  // 🔘 BUTTON: Tell time (only in monitoring mode)
  if (buttonState == LOW && currentState == STATE_MONITORING) {
    delay(50);  // Debounce
    if (digitalRead(buttonPin) == LOW) {
      if (millis() - lastTimeRequest > TIME_REQUEST_COOLDOWN) {
        requestTimeAnnouncement();
        lastTimeRequest = millis();
        delay(300);  // Extra debounce
      }
    }
  }
}

/* ============================================
   NETWORK FUNCTIONS
   ============================================ */

void sendFallAlertToLaptop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi not connected");
    return;
  }
  
  HTTPClient http;
  http.begin(laptopAlertUrl);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{\"event\":\"fall_detected\",\"timestamp\":" + String(millis()) + "}";
  
  int responseCode = http.POST(payload);
  if (responseCode > 0) {
    Serial.printf("✅ Laptop notified (HTTP %d)\n", responseCode);
  }
  
  http.end();
}

void triggerAudioAlert(const char* alertType) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(audioAlertUrl);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{\"object\":\"emergency\",\"distance\":0,\"type\":\"" + String(alertType) + "\"}";
  
  int responseCode = http.POST(payload);
  if (responseCode > 0) {
    Serial.printf("🔊 Audio: %s\n", alertType);
  }
  
  http.end();
}

/* ============================================
   STATE MACHINE
   ============================================ */

void handleMonitoringState() {
  if (checkForFall()) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  ⚠️  FALL DETECTED!  ⚠️           ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    currentState = STATE_COUNTDOWN;
    countdownStartTime = millis();
    emergencyCanceled = false;
    
    sendFallAlertToLaptop();
    triggerAudioAlert("fall_countdown");
    
    Serial.println("⏱️  15 SECOND COUNTDOWN");
    Serial.println("🔘 Press button to CANCEL\n");
  }
  
  // Handle joystick
  handleJoystickCommands();
}

void handleCountdownState() {
  unsigned long elapsed = millis() - countdownStartTime;
  unsigned long remaining = COUNTDOWN_TIME - elapsed;
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    Serial.printf("⏱️  Calling in %lu seconds...\n", remaining / 1000);
    lastPrint = millis();
  }
  
  if (checkCancelButton()) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  ✅ CANCELED BY USER               ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    currentState = STATE_PAUSED;
    pauseStartTime = millis();
    triggerAudioAlert("emergency_canceled");
    
    Serial.println("⏸️  Paused 15s\n");
    return;
  }
  
  if (elapsed >= COUNTDOWN_TIME) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  🚨 EMERGENCY CALL ACTIVATED 🚨    ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    currentState = STATE_CALLING;
    triggerAudioAlert("calling_emergency");
  }
}

void handleCallingState() {
  Serial.println("📞 EMERGENCY PROTOCOL\n");
  
  String smsMessage = "EMERGENCY ALERT: Fall detected! User may need assistance. Time: ";
  smsMessage += String(millis() / 1000);
  smsMessage += "s";
  
  sendSMS(emergencyPhone, smsMessage.c_str());
  delay(2000);
  
  makeCall(emergencyPhone);
  
  Serial.println("\n✅ Emergency completed");
  Serial.println("⏸️  Pausing 15s\n");
  
  currentState = STATE_PAUSED;
  pauseStartTime = millis();
}

void handlePausedState() {
  unsigned long elapsed = millis() - pauseStartTime;
  unsigned long remaining = (15000 - elapsed) / 1000;
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 5000) {
    Serial.printf("⏸️  Paused %lu more seconds\n", remaining);
    lastPrint = millis();
  }
  
  if (elapsed >= 15000) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  ✅ RESUMING MONITORING            ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    currentState = STATE_MONITORING;
    triggerAudioAlert("monitoring_resumed");
  }
}

/* ============================================
   SETUP
   ============================================ */

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║   🚑 FALL DETECTION SYSTEM 🚑        ║");
  Serial.println("╚═══════════════════════════════════════╝\n");
  
  // Joystick
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.println("✅ Joystick ready");
  
  // MPU6050
  Wire.begin();
  mpu.initialize();
  
  if (mpu.testConnection()) {
    Serial.println("✅ MPU6050 connected");
  } else {
    Serial.println("❌ MPU6050 FAILED!");
    while (true) delay(1000);
  }
  
  // UART to Ultrasonic ESP32
  ultrasonicSerial.begin(115200, SERIAL_8N1, ULTRASONIC_RX, ULTRASONIC_TX);
  Serial.println("✅ UART to ultrasonic ready");
  
  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("📡 WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi connected");
    Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("⚠️  WiFi failed - offline mode");
  }
  
  // SIM800L
  sim800_init();
  
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║  ✅ READY - MONITORING FALLS          ║");
  Serial.println("╚═══════════════════════════════════════╝\n");
  
  currentState = STATE_MONITORING;
}

/* ============================================
   MAIN LOOP
   ============================================ */

void loop() {
  switch (currentState) {
    case STATE_MONITORING:
      handleMonitoringState();
      delay(100);
      break;
      
    case STATE_COUNTDOWN:
      handleCountdownState();
      delay(100);
      break;
      
    case STATE_CALLING:
      handleCallingState();
      break;
      
    case STATE_PAUSED:
      handlePausedState();
      delay(1000);
      break;
  }
  
  // WiFi reconnection
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
    lastWiFiCheck = millis();
  }
}