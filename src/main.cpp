#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

constexpr char kConfigPath[] = "/flight-controller.json";
constexpr uint8_t kStatusLedPin = 2;
constexpr unsigned long kStatusIntervalMs = 500;
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr uint8_t kHmc5883lAddress = 0x1E;
constexpr uint8_t kQmc5883lAddress = 0x0D;
uint8_t kEscMotorPins[] = {25, 26, 27, 14};
constexpr uint8_t kEscPwmChannels[] = {0, 1, 2, 3};
constexpr uint32_t kEscPwmFrequency = 50;
constexpr uint8_t kEscPwmResolution = 16;
constexpr uint16_t kEscPwmSafeUs = 1000;
constexpr uint16_t kEscPwmTestUs = 1150;
constexpr uint16_t kEscPwmHighUs = 2000;
constexpr uint16_t kEscRadioIdleDeadbandUs = 15;
constexpr uint16_t kArmThreshold = 1000;
constexpr unsigned long kReceiverTimeoutMs = 300;

bool configurationLoaded = false;
bool motorOutputEnabled = false;
bool displayDetected = false;
const char* magnetometerModel = "not found";
String i2cDevices = "none";
bool statusLedOn = false;
unsigned long lastStatusUpdateMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastReceiverDisplayMs = 0;
unsigned long escTestUntilMs = 0;
uint16_t receiverChannels[16] = {992};
bool receiverSignalDetected = false;
unsigned long lastReceiverFrameMs = 0;
unsigned long lastReceiverLogMs = 0;
uint16_t motorOutputUs = kEscPwmSafeUs;
bool motorArmed = false;
uint8_t displaySdaPin = 5;
uint8_t displaySclPin = 4;
uint8_t sensorSdaPin = 5;
uint8_t sensorSclPin = 4;
uint8_t displayAddress = 0x3C;
uint8_t receiverRxPin = 16;
uint8_t receiverTxPin = 17;
uint32_t receiverBaud = 420000;
uint8_t rollChannelIndex = 0;
uint8_t pitchChannelIndex = 1;
uint8_t throttleChannelIndex = 2;
uint8_t armChannelIndex = 3;
bool rollInputReverse = false;
bool pitchInputReverse = false;
uint16_t receiverCenter = 992;
uint16_t receiverInputMin = 172;
uint16_t receiverInputMax = 1811;
uint16_t escSignalMinUs = 1000;
uint16_t escArmedIdleUs = 1000;
uint16_t escSignalStartUs = 1140;
uint16_t escSignalMaxUs = 1380;
uint8_t controlLevelPercent = 20;
bool mixerEnabled = true;
int16_t rollAuthorityUs = 76;
int16_t pitchAuthorityUs = 76;
int8_t motorRollSign[4] = {-1, -1, 1, 1};
int8_t motorPitchSign[4] = {1, -1, 1, -1};
float batteryNominalVoltage = 0.0F;
Adafruit_SSD1306 display(kDisplayWidth, kDisplayHeight, &Wire, -1);
HardwareSerial receiverSerial(2);
TwoWire sensorWire(1);
WebServer webServer(80);
String wifiSsid = "Fipik-01";
String wifiPassword = "fipik-config";

int8_t readDirection(JsonVariantConst value, int8_t fallback) {
  const char* direction = value | "";
  if (strcmp(direction, "increase") == 0) {
    return 1;
  }
  if (strcmp(direction, "decrease") == 0) {
    return -1;
  }
  return fallback;
}

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
      for (uint8_t index = 0; index < 22 && channelIndex < 16; ++index) {
        bits |= static_cast<uint32_t>(payload[index]) << bitCount;
        bitCount += 8;
        while (bitCount >= 11 && channelIndex < 16) {
          receiverChannels[channelIndex++] = bits & 0x07FF;
          bits >>= 11;
          bitCount -= 11;
        }
      }
      receiverSignalDetected = true;
      lastReceiverFrameMs = millis();
      const unsigned long nowMs = millis();
      if (nowMs - lastReceiverLogMs >= 500) {
        lastReceiverLogMs = nowMs;
        Serial.print("RC channels:");
        for (uint8_t index = 0; index < 16; ++index) {
          Serial.printf(" %u", receiverChannels[index]);
        }
        Serial.println();
      }
    }
    frameSize = 0;
  }
}

bool deviceResponds(TwoWire& bus, uint8_t address) {
  bus.beginTransmission(address);
  return bus.endTransmission() == 0;
}

bool scanI2cBus(TwoWire& bus, String& devices, uint8_t expectedAddress) {
  bool expectedDeviceFound = false;
  bool anyDeviceFound = false;
  devices = "";

  for (uint8_t address = 1; address < 127; ++address) {
    if (deviceResponds(bus, address)) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      anyDeviceFound = true;
      expectedDeviceFound = expectedDeviceFound || address == expectedAddress;
      if (devices.length() > 0) {
        devices += " ";
      }
      char addressText[3];
      snprintf(addressText, sizeof(addressText), "%02X", address);
      devices += addressText;
    }
  }

  if (!anyDeviceFound) {
    Serial.println("No I2C devices found.");
    devices = "none";
  }

  return expectedDeviceFound;
}

void detectMagnetometer() {
  if (deviceResponds(sensorWire, kHmc5883lAddress)) {
    magnetometerModel = "HMC5883L";
  } else if (deviceResponds(sensorWire, kQmc5883lAddress)) {
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
  motorOutputEnabled = config["safety"]["motor_output_enabled"] | false;
  wifiSsid = config["wifi"]["ssid"] | wifiSsid;
  wifiPassword = config["wifi"]["password"] | wifiPassword;
  JsonObject pins = config["pins"];
  displaySdaPin = pins["i2c"]["display_sda"] | displaySdaPin;
  displaySclPin = pins["i2c"]["display_scl"] | displaySclPin;
  sensorSdaPin = pins["i2c"]["sensor_sda"] | sensorSdaPin;
  sensorSclPin = pins["i2c"]["sensor_scl"] | sensorSclPin;
  displayAddress = config["display"]["i2c_address"] | displayAddress;
  receiverRxPin = pins["receiver_rx900"]["rx"] | receiverRxPin;
  receiverTxPin = pins["receiver_rx900"]["tx"] | receiverTxPin;
  receiverBaud = pins["receiver_rx900"]["baud"] | receiverBaud;

  JsonObject esc = config["esc"];
  escSignalMinUs = esc["motor_signal_min_us"] | escSignalMinUs;
  escArmedIdleUs = esc["armed_idle_us"] | escArmedIdleUs;
  escSignalStartUs = esc["motor_signal_start_us"] | escSignalStartUs;
  escSignalMaxUs = esc["motor_signal_max_us"] | escSignalMaxUs;
  controlLevelPercent = esc["control_level_percent"] | controlLevelPercent;
  rollAuthorityUs = static_cast<int16_t>((escSignalMaxUs - escSignalMinUs) *
                                         controlLevelPercent / 100U);
  pitchAuthorityUs = rollAuthorityUs;

  JsonObject radio = config["radio"];
  rollChannelIndex = radio["roll_channel"] | rollChannelIndex;
  pitchChannelIndex = radio["pitch_channel"] | pitchChannelIndex;
  throttleChannelIndex = radio["throttle_channel"] | throttleChannelIndex;
  armChannelIndex = radio["arm_channel"] | armChannelIndex;
  rollInputReverse = radio["roll_reverse"] | rollInputReverse;
  pitchInputReverse = radio["pitch_reverse"] | pitchInputReverse;
  receiverCenter = radio["center"] | receiverCenter;
  receiverInputMin = radio["input_min"] | receiverInputMin;
  receiverInputMax = radio["input_max"] | receiverInputMax;

  JsonArray motors = config["motors"];
  for (uint8_t index = 0; index < 4; ++index) {
    JsonObject motor = motors[index];
    const char* motorName = motor["name"] | "";
    if (strcmp(motorName, "M1") == 0) kEscMotorPins[index] = pins["esc"]["M1"] | kEscMotorPins[index];
    if (strcmp(motorName, "M2") == 0) kEscMotorPins[index] = pins["esc"]["M2"] | kEscMotorPins[index];
    if (strcmp(motorName, "M3") == 0) kEscMotorPins[index] = pins["esc"]["M3"] | kEscMotorPins[index];
    if (strcmp(motorName, "M4") == 0) kEscMotorPins[index] = pins["esc"]["M4"] | kEscMotorPins[index];
    motorRollSign[index] = readDirection(motor["roll"], motorRollSign[index]);
    motorPitchSign[index] = readDirection(motor["pitch"], motorPitchSign[index]);
  }

  if (nominalVoltage <= 0.0F || capacityMah == 0) {
    Serial.println("Battery configuration is incomplete.");
    return false;
  }

  batteryNominalVoltage = nominalVoltage;
  Serial.printf("Vehicle: %s\n", config["vehicle"]["name"] | "unnamed");
  Serial.printf("Battery: %.1f V, %lu mAh\n", nominalVoltage, capacityMah);
  Serial.printf("ESC: %s\n", config["esc"]["model"] | "unknown");
  Serial.printf("Motor output: %s\n", motorOutputEnabled ? "enabled" : "disabled");
  Serial.printf("Mixer: %s, level=%u%%\n", mixerEnabled ? "quad X" : "off",
                controlLevelPercent);

  if (motorOutputEnabled) {
    Serial.println("Motor output is not implemented in this prototype.");
  }

  return true;
}

void handleConfigGet() {
  File configFile = LittleFS.open(kConfigPath, "r");
  if (!configFile) {
    webServer.send(500, "application/json", "{\"error\":\"config_missing\"}");
    return;
  }
  webServer.streamFile(configFile, "application/json");
  configFile.close();
}

void handleConfigPut() {
  JsonDocument config;
  const DeserializationError error = deserializeJson(config, webServer.arg("plain"));
  if (error || !config.is<JsonObject>()) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }
  File configFile = LittleFS.open(kConfigPath, "w");
  if (!configFile) {
    webServer.send(500, "application/json", "{\"error\":\"config_write_failed\"}");
    return;
  }
  serializeJson(config, configFile);
  configFile.close();
  webServer.send(200, "application/json", "{\"saved\":true,\"restarting\":true}");
  delay(300);
  ESP.restart();
}

void initializeWifiApi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(wifiSsid.c_str(), wifiPassword.c_str());
  webServer.on("/api/config", HTTP_GET, handleConfigGet);
  webServer.on("/api/config", HTTP_PUT, handleConfigPut);
  webServer.on("/api/health", HTTP_GET, []() {
    webServer.send(200, "application/json", "{\"status\":\"ok\",\"device\":\"Fipik-01\"}");
  });
  webServer.begin();
  Serial.printf("WiFi AP: %s, address: %s\n", wifiSsid.c_str(),
                WiFi.softAPIP().toString().c_str());
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
                kEscMotorPins[0], kEscMotorPins[1], kEscMotorPins[2], kEscMotorPins[3]);
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
  motorArmed = motorOutputEnabled && linkActive && receiverChannels[armChannelIndex] > kArmThreshold;
  int16_t rollCorrectionUs = 0;
  int16_t pitchCorrectionUs = 0;
  if (!motorArmed) {
    motorOutputUs = kEscPwmSafeUs;
  } else {
    const uint16_t throttle = receiverChannels[throttleChannelIndex];
    const long mappedOutput = map(throttle, receiverInputMin, receiverInputMax,
                                  escSignalMinUs, escSignalMaxUs);
    motorOutputUs = static_cast<uint16_t>(constrain(
        mappedOutput, static_cast<long>(escSignalMinUs),
        static_cast<long>(escSignalMaxUs)));
    if (motorOutputUs <= kEscPwmSafeUs + kEscRadioIdleDeadbandUs) {
      motorOutputUs = escArmedIdleUs;
    }

    if (mixerEnabled && motorOutputUs > kEscPwmSafeUs + kEscRadioIdleDeadbandUs) {
      const int16_t rollInput = static_cast<int16_t>(receiverChannels[rollChannelIndex]) -
                                static_cast<int16_t>(receiverCenter);
      const int16_t pitchInput = static_cast<int16_t>(receiverChannels[pitchChannelIndex]) -
                                 static_cast<int16_t>(receiverCenter);
      rollCorrectionUs = map(rollInputReverse ? -rollInput : rollInput,
                             static_cast<int16_t>(receiverInputMin) - receiverCenter,
                             static_cast<int16_t>(receiverInputMax) - receiverCenter,
                             -rollAuthorityUs, rollAuthorityUs);
      pitchCorrectionUs = map(pitchInputReverse ? -pitchInput : pitchInput,
                              static_cast<int16_t>(receiverInputMin) - receiverCenter,
                              static_cast<int16_t>(receiverInputMax) - receiverCenter,
                              -pitchAuthorityUs, pitchAuthorityUs);
    }
  }
  for (uint8_t motorIndex = 0; motorIndex < 4; ++motorIndex) {
    int16_t mixedOutput = static_cast<int16_t>(motorOutputUs) +
                          rollCorrectionUs * motorRollSign[motorIndex] +
                          pitchCorrectionUs * motorPitchSign[motorIndex];
    mixedOutput = constrain(mixedOutput, static_cast<int16_t>(kEscPwmSafeUs),
                            static_cast<int16_t>(escSignalMaxUs));
    writeEscPwm(kEscPwmChannels[motorIndex], static_cast<uint16_t>(mixedOutput));
  }
}

bool initializeDisplay() {
  Wire.begin(displaySdaPin, displaySclPin);
  Wire.setClock(100000);

  Serial.printf("OLED I2C: SDA=%u, SCL=%u\n", displaySdaPin, displaySclPin);
  displayDetected = scanI2cBus(Wire, i2cDevices, displayAddress);

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
  display.printf("ARM:%s CH5:%u", motorArmed ? "ON" : "OFF",
                 receiverChannels[armChannelIndex]);
  display.display();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  receiverSerial.begin(receiverBaud, SERIAL_8N1, receiverRxPin, receiverTxPin);
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
  sensorWire.begin(sensorSdaPin, sensorSclPin);
  sensorWire.setClock(100000);
  Serial.printf("Sensors I2C: SDA=%u, SCL=%u\n", sensorSdaPin, sensorSclPin);
  scanI2cBus(sensorWire, i2cDevices, kQmc5883lAddress);
  detectMagnetometer();
  if (oledInitialized) {
    renderDisplay();
    Serial.println("OLED status screen is ready.");
  }
  initializeEscSafeOutput();
  initializeWifiApi();
  Serial.println("ESC output is limited to minimum throttle.");
}

void loop() {
  const unsigned long nowMs = millis();

  updateReceiver();
  updateMotorFromReceiver();
  webServer.handleClient();

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
    displayDetected = scanI2cBus(Wire, i2cDevices, displayAddress);
    scanI2cBus(sensorWire, i2cDevices, kQmc5883lAddress);
    detectMagnetometer();
    if (displayDetected) {
      renderDisplay();
    }
    Serial.printf("Safe test mode is running. OLED: %s, MAG: %s\n",
                  displayDetected ? "ready" : "not detected", magnetometerModel);
  }
}
