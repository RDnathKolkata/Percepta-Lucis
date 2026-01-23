/*
 ESP32 Fall Detection with BLE (Bluetooth Low Energy) 
 Uses BLE instead of Classic Bluetooth for lower power consumption
 
 Hardware:
 - ESP32 DevKit
 - MPU6050 (I2C: SDA=GPIO21, SCL=GPIO22)
 - Joystick Module (VRx=GPIO34, VRy=GPIO35, SW=GPIO32)
 
 Phone Integration:
 - BLE connection to Android/iOS app
 - App receives fall alerts and makes emergency calls
 - Lower power than Classic Bluetooth
 
 Sim lagbe na 
 */

#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

/* USER CONFIGURATION */

// WiFi creds
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// Server endpoints
const char* laptopAlertUrl = "http://192.168.1.100:8000/fall_alert";
const char* audioAlertUrl = "http://192.168.1.101/alert";

// BLE UUIDs (DO NOT CHANGE FOR THE LOVE OF GOD)
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Pin definitions
const int VRxPin = 34;
const int VRyPin = 35;
const int buttonPin = 32;
const int ULTRASONIC_TX = 26;
const int ULTRASONIC_RX = 27;

// Fall detection parameters
const float FALL_THRESHOLD = 2.5;
const float FREEFALL_THRESHOLD = 0.5;
const unsigned long COUNTDOWN_TIME = 15000;

// Joystick parameters
const int ADC_CENTER = 2048;
const int DEADZONE = 200;
const int UPPER_THRESHOLD = ADC_CENTER + DEADZONE;
const int LOWER_THRESHOLD = ADC_CENTER - DEADZONE;

/* GLOBALIZATION IN CODING (VARIABLES) */

MPU6050 mpu;
HardwareSerial ultrasonicSerial(2);

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool phoneConnected = false;

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

/* BLE CALLBACK CLASS */

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    phoneConnected = true;
    Serial.println("📱 Phone connected via BLE");
  }
  
  void onDisconnect(BLEServer* pServer) {
    phoneConnected = false;
    Serial.println("📱 Phone disconnected from BLE");
    // Restart advertising so phone can reconnect
    BLEDevice::startAdvertising();
    Serial.println("📡 BLE advertising restarted");
  }
};

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
      String message = String(value.c_str());
      Serial.printf("📱 Received from phone: %s\n", message.c_str());
      
      // Phone can send acknowledgments
      if (message == "ACK:CALLING") {
        Serial.println("✅ Phone confirmed: Making call BACHAO BACHAO");
      }
      else if (message == "ACK:CANCELED") {
        Serial.println("✅ Phone confirmed: Canceled");
      }
      else if (message == "STATUS:CALL_ENDED") {
        Serial.println("📞 Phone: Call ended");
      }
    }
  }
};

/* BLE FUNCTIONS */

void setupBLE() {
  Serial.println("🔵 Initializing BLE...");
  
  // Initialize BLE
  BLEDevice::init("FallDetector");
  
  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  // Create BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Create BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );
  
  // Add callbacks
  pCharacteristic->setCallbacks(new CharacteristicCallbacks());
  
  // Add descriptor for notifications
  pCharacteristic->addDescriptor(new BLE2902());
  
  // Start the service
  pService->start();
  
  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("✅ BLE started - Device name: 'FallDetector'");
  Serial.println("📱 Waiting for phone connection...");
}

void sendToBLE(const char* message) {
  if (!phoneConnected) {
    Serial.println("⚠️ Phone not connected - cannot send alert!");
    return;
  }
  
  pCharacteristic->setValue(message);
  pCharacteristic->notify();
  
  Serial.printf("📱 BLE sent: %s\n", message);
}

void sendJSONAlert(const char* type, int countdown = 0) {
  if (!phoneConnected) return;
  
  // Create JSON msg
  String json = "{\"type\":\"" + String(type) + "\"";
  json += ",\"timestamp\":" + String(millis());
  
  if (countdown > 0) {
    json += ",\"countdown\":" + String(countdown);
  }
  
  json += "}";
  
  sendToBLE(json.c_str());
}

/*  FALL DETECTION */

bool checkForFall() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  float AccX = ax / 16384.0;
  float AccY = ay / 16384.0;
  float AccZ = az / 16384.0;
  
  float AccMagnitude = sqrt(AccX * AccX + AccY * AccY + AccZ * AccZ);
  
  if (millis() - lastFallCheck > 1000) {
    Serial.printf("📊 Accel: %.2fg | Phone: %s | State: ", 
                  AccMagnitude, 
                  phoneConnected ? "Connected" : "Disconnected");
    
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
      Serial.println("🔘 Cancel button pressed!");
      return true;
    }
  }
  return false;
}

void handleJoystickCommands() {
  int xVal = analogRead(VRxPin);
  int yVal = analogRead(VRyPin);
  
  static unsigned long lastCommand = 0;
  if (millis() - lastCommand < 500) return;
  
  // Send commands to Ultrasonic ESP32
  if (yVal > UPPER_THRESHOLD) {
    ultrasonicSerial.println("CMD_RANGE_UP");
    Serial.println("📤 → Ultrasonic: RANGE_UP");
    lastCommand = millis();
  }
  else if (yVal < LOWER_THRESHOLD) {
    ultrasonicSerial.println("CMD_RANGE_DOWN");
    Serial.println("📤 → Ultrasonic: RANGE_DOWN");
    lastCommand = millis();
  }
  
  if (xVal > UPPER_THRESHOLD) {
    ultrasonicSerial.println("CMD_FOCUS_RIGHT");
    Serial.println("📤 → Ultrasonic: FOCUS_RIGHT");
    lastCommand = millis();
  }
  else if (xVal < LOWER_THRESHOLD) {
    ultrasonicSerial.println("CMD_FOCUS_LEFT");
    Serial.println("📤 → Ultrasonic: FOCUS_LEFT");
    lastCommand = millis();
  }
}

/* ============================================
   NETWORK FUNCTIONS (OPTIONAL)
   ============================================ */

void sendFallAlertToLaptop() {
  if (WiFi.status() != WL_CONNECTED) return;
  
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

/* STATE MACHINE */

void handleMonitoringState() {
  if (checkForFall()) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  ⚠️  FALL DETECTED!  ⚠️           ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    currentState = STATE_COUNTDOWN;
    countdownStartTime = millis();
    emergencyCanceled = false;
    
    // Alert all systems
    sendFallAlertToLaptop();
    triggerAudioAlert("fall_countdown");
    sendJSONAlert("fall_detected", 15);
    
    Serial.println("⏱️  15 SECOND COUNTDOWN");
    Serial.println("🔘 Press button to CANCEL\n");
  }
  
  handleJoystickCommands();
}

void handleCountdownState() {
  unsigned long elapsed = millis() - countdownStartTime;
  unsigned long remaining = (COUNTDOWN_TIME - elapsed) / 1000;
  
  static unsigned long lastPrint = 0;
  static unsigned long lastNotify = 0;
  
  if (millis() - lastPrint >= 1000) {
    Serial.printf("⏱️  Calling in %lu seconds...\n", remaining);
    lastPrint = millis();
  }
  
  // Send countdown updates to phone every 3 seconds
  if (millis() - lastNotify >= 3000) {
    sendJSONAlert("countdown", remaining);
    lastNotify = millis();
  }
  
  if (checkCancelButton()) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  ✅ CANCELED BY USER               ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    currentState = STATE_PAUSED;
    pauseStartTime = millis();
    
    triggerAudioAlert("emergency_canceled");
    sendJSONAlert("canceled");
    
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
  Serial.println("📱 SENDING EMERGENCY TO PHONE\n");
  
  /* Send emergency command - phone app will:
    1. Make call to emergency contact
    2. Send SMS with GPS location*/
  sendJSONAlert("emergency_call");
  
  delay(2000);
  
  Serial.println("✅ Emergency activated");
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
    sendJSONAlert("monitoring_resumed");
  }
}

/* ============================================
   SETUP
   ============================================ */

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║   🚑 FALL DETECTION (BLE) 🚑         ║");
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
  
  // Setup BLE
  setupBLE();
  
  // UART to Ultrasonic ESP32
  ultrasonicSerial.begin(115200, SERIAL_8N1, ULTRASONIC_RX, ULTRASONIC_TX);
  Serial.println("✅ UART to ultrasonic ready");
  
  // WiFi (optional)
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
    Serial.println("✅ WiFi OK");
    Serial.printf("📍 %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("⚠️  WiFi failed");
  }
  
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