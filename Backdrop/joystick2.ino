const int VRxPin = 34;       // Joystick X-axis ADC pin
const int VRyPin = 35;       // Joystick Y-axis ADC pin
const int buttonPin = 25;    // Joystick push button pin

const int ADC_CENTER = 2048;
const int DEADZONE = 200;

const int UPPER_THRESHOLD = ADC_CENTER + DEADZONE;
const int LOWER_THRESHOLD = ADC_CENTER - DEADZONE;

void setup() {
  Serial.begin(115200); // Send via UART to receiver ESP32
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.println("Joystick control started");
}

void loop() {
  int xVal = analogRead(VRxPin);
  int yVal = analogRead(VRyPin);
  int buttonState = digitalRead(buttonPin);

  if (yVal > UPPER_THRESHOLD) {
    Serial.println("CMD_TIME");
  }
  else if (yVal < LOWER_THRESHOLD) {
    Serial.println("CMD_VIB_TOGGLE");
  }

  if (xVal > UPPER_THRESHOLD) {
    Serial.println("CMD_RANGE_DOWN");
  }
  else if (xVal < LOWER_THRESHOLD) {
    Serial.println("CMD_RANGE_UP");
  }

  if (buttonState == LOW) {
    Serial.println("CMD_FALL_TOGGLE");
    delay(300); // debounce
  }

  delay(100);
}
