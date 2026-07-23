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

bool configurationLoaded = false;
bool displayDetected = false;
bool statusLedOn = false;
unsigned long lastStatusUpdateMs = 0;
unsigned long lastHeartbeatMs = 0;
uint8_t displaySdaPin = 5;
uint8_t displaySclPin = 4;
uint8_t displayAddress = 0x3C;
float batteryNominalVoltage = 0.0F;
Adafruit_SSD1306 display(kDisplayWidth, kDisplayHeight, &Wire, -1);

bool scanI2cBus() {
  bool expectedDeviceFound = false;
  bool anyDeviceFound = false;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      anyDeviceFound = true;
      expectedDeviceFound = expectedDeviceFound || address == displayAddress;
    }
  }

  if (!anyDeviceFound) {
    Serial.println("No I2C devices found.");
  }

  return expectedDeviceFound;
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
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("FIPIK FC");
  display.drawFastHLine(0, 18, kDisplayWidth, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 25);
  display.println("SAFE TEST MODE");
  display.printf("BAT: %.1f V\n", batteryNominalVoltage);
  display.println("ESC: OFF");
  display.printf("CFG: %s", configurationLoaded ? "OK" : "ERROR");
  display.display();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
  delay(300);
  Serial.println("\nFipik flight controller: safe test mode");

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
    return;
  }

  configurationLoaded = loadConfiguration();
  if (initializeDisplay()) {
    renderDisplay();
    Serial.println("OLED status screen is ready.");
  }
  Serial.println("ESC output remains disabled.");
}

void loop() {
  const unsigned long nowMs = millis();

  if (nowMs - lastStatusUpdateMs >= kStatusIntervalMs) {
    lastStatusUpdateMs = nowMs;
    statusLedOn = !statusLedOn;
    digitalWrite(kStatusLedPin, statusLedOn ? HIGH : LOW);
  }

  if (configurationLoaded && nowMs - lastHeartbeatMs >= 5000) {
    lastHeartbeatMs = nowMs;
    displayDetected = scanI2cBus();
    Serial.printf("Safe test mode is running. OLED: %s\n",
                  displayDetected ? "ready" : "not detected");
  }
}
