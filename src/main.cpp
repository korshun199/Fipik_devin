#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>

namespace {

constexpr char kConfigPath[] = "/flight-controller.json";
constexpr uint8_t kStatusLedPin = 2;
constexpr unsigned long kStatusIntervalMs = 500;
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr uint8_t kHmc5883lAddress = 0x1E;
constexpr uint8_t kQmc5883lAddress = 0x0D;
constexpr uint8_t kEscMotor1Pin = 25;
constexpr uint8_t kEscMotor2Pin = 26;
constexpr uint8_t kEscMotor3Pin = 27;
constexpr uint8_t kEscMotor4Pin = 14;
constexpr uint8_t kEscMotorPins[] = {kEscMotor1Pin, kEscMotor2Pin, kEscMotor3Pin, kEscMotor4Pin};
constexpr uint8_t kEscPwmChannels[] = {0, 1, 2, 3};
constexpr uint32_t kEscPwmFrequency = 50;
constexpr uint8_t kEscPwmResolution = 16;
constexpr uint16_t kEscPwmSafeUs = 1000;
constexpr uint16_t kEscPwmTestUs = 1150;
constexpr uint16_t kEscPwmHighUs = 2000;
constexpr uint16_t kEscRadioMaxUs = 1380;
constexpr uint16_t kEscRadioStartUs = 1140;
constexpr uint16_t kThrottleStartInput = 1600;
constexpr uint16_t kEscRadioIdleDeadbandUs = 15;
constexpr uint8_t kArmChannelIndex = 3;
constexpr uint16_t kArmThreshold = 1000;
constexpr unsigned long kReceiverTimeoutMs = 300;
constexpr uint8_t kReceiverRxPin = 16;
constexpr uint8_t kReceiverTxPin = 17;
constexpr uint32_t kReceiverBaud = 420000;

bool configurationLoaded = false;
bool displayDetected = false;
const char* magnetometerModel = "not found";
String i2cDevices = "none";
bool statusLedOn = false;
unsigned long lastStatusUpdateMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastReceiverDisplayMs = 0;
unsigned long escTestUntilMs = 0;
uint16_t receiverChannels[4] = {992, 992, 992, 172};
bool receiverSignalDetected = false;
unsigned long lastReceiverFrameMs = 0;
uint16_t motorOutputUs = kEscPwmSafeUs;
bool motorArmed = false;
uint8_t displaySdaPin = 5;
uint8_t displaySclPin = 4;
uint8_t displayAddress = 0x3C;
float batteryNominalVoltage = 0.0F;
Adafruit_SSD1306 display(kDisplayWidth, kDisplayHeight, &Wire, -1);
HardwareSerial receiverSerial(2);

void updateReceiver() {
  static uint8_t frame[64];
  static uint8_t frameSize = 0;

  while (receiverSerial.available() > 0) {
    const uint8_t value = receiverSerial.read();
    if (frameSize == 0) {
      frame[frameSize++] = value;
      continue;
    }
    if (frameSize == 1) {
      if (value < 2 || value > 62) {
        frameSize = 0;
        continue;
      }
      frame[frameSize++] = value;
      continue;
    }
    frame[frameSize++] = value;
    if (frameSize < static_cast<uint8_t>(frame[1]) + 2) {
      continue;
    }

    if (frame[2] == 0x16 && frame[1] >= 23) {
      const uint8_t* payload = &frame[3];
      uint32_t bits = 0;
      uint8_t bitCount = 0;
      uint8_t channelIndex = 0;
      for (uint8_t index = 0; index < 22 && channelIndex < 4; ++index) {
        bits |= static_cast<uint32_t>(payload[index]) << bitCount;
        bitCount += 8;
        while (bitCount >= 11 && channelIndex < 4) {
          receiverChannels[channelIndex++] = bits & 0x07FF;
          bits >>= 11;
          bitCount -= 11;
        }
      }
      receiverSignalDetected = true;
      lastReceiverFrameMs = millis();
      Serial.printf("RC channels: %u %u %u %u\n", receiverChannels[0], receiverChannels[1],
                    receiverChannels[2], receiverChannels[3]);
    }
    frameSize = 0;
  }
}

bool deviceResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool scanI2cBus() {
  bool expectedDeviceFound = false;
  bool anyDeviceFound = false;
  i2cDevices = "";

  for (uint8_t address = 1; address < 127; ++address) {
    if (deviceResponds(address)) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      anyDeviceFound = true;
      expectedDeviceFound = expectedDeviceFound || address == displayAddress;
      if (i2cDevices.length() > 0) {
        i2cDevices += " ";
      }
      char addressText[3];
      snprintf(addressText, sizeof(addressText), "%02X", address);
      i2cDevices += addressText;
    }
  }

  if (!anyDeviceFound) {
    Serial.println("No I2C devices found.");
    i2cDevices = "none";
  }

  return expectedDeviceFound;
}

void detectMagnetometer() {
  if (deviceResponds(kHmc5883lAddress)) {
    magnetometerModel = "HMC5883L";
  } else if (deviceResponds(kQmc5883lAddress)) {
    magnetometerModel = "QMC5883L";
  }

  Serial.printf("Magnetometer: %s\n", magnetometerModel);
}

bool loadConfiguration() {
  File configFile = LittleFS.open(kConfigPath, "r");
  if (!configFile) {
    Serial.println("Configuration file is missing.");
    return false;
  }

  JsonDocument config;
  const DeserializationError error = deserializeJson(config, configFile);
  configFile.close();

  if (error) {
    Serial.printf("Configuration is invalid: %s\n", error.c_str());
    return false;
  }

  const float nominalVoltage = config["battery"]["nominal_voltage_v"] | 0.0F;
  const unsigned long capacityMah = config["battery"]["capacity_mah"] | 0UL;
  const bool motorOutputEnabled = config["safety"]["motor_output_enabled"] | false;
  displaySdaPin = config["display"]["i2c_sda_pin"] | displaySdaPin;
  displaySclPin = config["display"]["i2c_scl_pin"] | displaySclPin;
  displayAddress = config["display"]["i2c_address"] | displayAddress;

  if (nominalVoltage <= 0.0F || capacityMah == 0) {
    Serial.println("Battery configuration is incomplete.");
    return false;
  }

  batteryNominalVoltage = nominalVoltage;
  Serial.printf("Vehicle: %s\n", config["vehicle"]["name"] | "unnamed");
  Serial.printf("Battery: %.1f V, %lu mAh\n", nominalVoltage, capacityMah);
  Serial.printf("ESC: %s\n", config["esc"]["model"] | "unknown");
  Serial.printf("Motor output: %s\n", motorOutputEnabled ? "enabled" : "disabled");

  if (motorOutputEnabled) {
    Serial.println("Motor output is not implemented in this prototype.");
  }

  return true;
}

void writeEscPwm(uint8_t channel, uint16_t pulseUs) {
  const uint32_t duty = (static_cast<uint32_t>(pulseUs) * 65535UL) / 20000UL;
  ledcWrite(channel, duty);
}

void initializeEscSafeOutput() {
  for (uint8_t index = 0; index < 4; ++index) {
    ledcSetup(kEscPwmChannels[index], kEscPwmFrequency, kEscPwmResolution);
    ledcAttachPin(kEscMotorPins[index], kEscPwmChannels[index]);
    writeEscPwm(kEscPwmChannels[index], kEscPwmSafeUs);
  }
  Serial.printf("ESC M1-M4 safe PWM: GPIO%u, GPIO%u, GPIO%u, GPIO%u\n",
                kEscMotor1Pin, kEscMotor2Pin, kEscMotor3Pin, kEscMotor4Pin);
}

void handleEscTest() {
  if (Serial.available() > 0) {
    const char command = Serial.read();
    if (command == 'h') {
      writeEscPwm(kEscPwmChannels[0], kEscPwmHighUs);
      Serial.printf("ESC M1 calibration high: %u us\n", kEscPwmHighUs);
      return;
    }
    if (command == 'l') {
      writeEscPwm(kEscPwmChannels[0], kEscPwmSafeUs);
      Serial.printf("ESC M1 calibration low: %u us\n", kEscPwmSafeUs);
      return;
    }
    if (command != 't' && command != 'm') {
      return;
    }
    const uint32_t durationMs = command == 'm' ? 60000UL : 1000UL;
    writeEscPwm(kEscPwmChannels[3], kEscPwmTestUs);
    delay(durationMs);
    writeEscPwm(kEscPwmChannels[3], kEscPwmSafeUs);
    Serial.printf("ESC M4 test: PWM %u us for %lu ms, returned to %u us\n",
                  kEscPwmTestUs, durationMs, kEscPwmSafeUs);
  }
}

void updateMotorFromReceiver() {
  const bool linkActive = receiverSignalDetected &&
                          millis() - lastReceiverFrameMs <= kReceiverTimeoutMs;
  motorArmed = linkActive && receiverChannels[kArmChannelIndex] > kArmThreshold;
  if (!motorArmed) {
    motorOutputUs = kEscPwmSafeUs;
  } else {
    const uint16_t throttle = receiverChannels[2];
    long mappedOutput = 0;
    if (throttle >= kThrottleStartInput) {
      mappedOutput = map(throttle, 1811, kThrottleStartInput,
                         kEscPwmSafeUs, kEscRadioStartUs);
    } else {
      mappedOutput = map(throttle, kThrottleStartInput, 172,
                         kEscRadioStartUs, kEscRadioMaxUs);
    }
    motorOutputUs = static_cast<uint16_t>(constrain(
        mappedOutput, static_cast<long>(kEscPwmSafeUs),
        static_cast<long>(kEscRadioMaxUs)));
    if (motorOutputUs <= kEscPwmSafeUs + kEscRadioIdleDeadbandUs) {
      motorOutputUs = kEscPwmSafeUs;
    }
  }
  writeEscPwm(kEscPwmChannels[3], motorOutputUs);
}

bool initializeDisplay() {
  Wire.begin(displaySdaPin, displaySclPin);
  Wire.setClock(100000);

  Serial.printf("OLED I2C: SDA=%u, SCL=%u\n", displaySdaPin, displaySclPin);
  displayDetected = scanI2cBus();

  if (!displayDetected) {
    Serial.printf("OLED was not found at 0x%02X.\n", displayAddress);
    return false;
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, displayAddress, false, false)) {
    Serial.println("OLED initialization failed.");
    return false;
  }

  return true;
}

void renderDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("FIPIK FC | SAFE");
  display.drawFastHLine(0, 9, kDisplayWidth, SSD1306_WHITE);
  display.setCursor(0, 14);
  display.printf("I2C: %s\n", i2cDevices.c_str());
  display.printf("MAG: %s\n", magnetometerModel);
  display.printf("RC:%s A:%s\n", receiverSignalDetected ? "OK" : "--",
                 motorArmed ? "ON" : "OFF");
  display.printf("R%u P%u\n", receiverChannels[0], receiverChannels[1]);
  display.printf("Y%u T%u\n", receiverChannels[2], receiverChannels[3]);
  display.printf("M4 D%u %uus", kEscMotor4Pin, motorOutputUs);
  display.display();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  receiverSerial.begin(kReceiverBaud, SERIAL_8N1, kReceiverRxPin, kReceiverTxPin);
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
  delay(300);
  Serial.println("\nFipik flight controller: safe test mode");

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
    return;
  }

  configurationLoaded = loadConfiguration();
  const bool oledInitialized = initializeDisplay();
  detectMagnetometer();
  if (oledInitialized) {
    renderDisplay();
    Serial.println("OLED status screen is ready.");
  }
  initializeEscSafeOutput();
  Serial.println("ESC output is limited to minimum throttle.");
}

void loop() {
  const unsigned long nowMs = millis();

  updateReceiver();
  updateMotorFromReceiver();

  if (nowMs - lastReceiverDisplayMs >= 100) {
    lastReceiverDisplayMs = nowMs;
    if (displayDetected) {
      renderDisplay();
    }
  }

  handleEscTest();

  if (nowMs - lastStatusUpdateMs >= kStatusIntervalMs) {
    lastStatusUpdateMs = nowMs;
    statusLedOn = !statusLedOn;
    digitalWrite(kStatusLedPin, statusLedOn ? HIGH : LOW);
  }

  if (configurationLoaded && nowMs - lastHeartbeatMs >= 5000) {
    lastHeartbeatMs = nowMs;
    displayDetected = scanI2cBus();
    detectMagnetometer();
    if (displayDetected) {
      renderDisplay();
    }
    Serial.printf("Safe test mode is running. OLED: %s, MAG: %s\n",
                  displayDetected ? "ready" : "not detected", magnetometerModel);
  }
}
