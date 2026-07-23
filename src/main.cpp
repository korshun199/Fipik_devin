#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include "driver/rmt.h"

namespace {

constexpr char kConfigPath[] = "/flight-controller.json";
constexpr uint8_t kStatusLedPin = 2;
constexpr unsigned long kStatusIntervalMs = 500;
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr uint8_t kHmc5883lAddress = 0x1E;
constexpr uint8_t kQmc5883lAddress = 0x0D;
constexpr uint8_t kEscMotor1Pin = 25;
constexpr rmt_channel_t kEscRmtChannel = RMT_CHANNEL_0;
constexpr uint16_t kDshotThrottleTest = 300;

bool configurationLoaded = false;
bool displayDetected = false;
const char* magnetometerModel = "not found";
String i2cDevices = "none";
bool statusLedOn = false;
unsigned long lastStatusUpdateMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long escTestUntilMs = 0;
unsigned long lastDshotMs = 0;
uint8_t displaySdaPin = 5;
uint8_t displaySclPin = 4;
uint8_t displayAddress = 0x3C;
float batteryNominalVoltage = 0.0F;
Adafruit_SSD1306 display(kDisplayWidth, kDisplayHeight, &Wire, -1);

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

void sendDshotPacket(uint16_t throttle) {
  const uint16_t value = (throttle << 1);
  uint16_t checksum = value;
  uint16_t packet = (value << 4) | (checksum ^ (checksum >> 4) ^ (checksum >> 8)) & 0x0F;
  rmt_item32_t items[16] = {};

  for (uint8_t bit = 0; bit < 16; ++bit) {
    const bool one = packet & (1U << (15 - bit));
    items[bit].level0 = 1;
    items[bit].duration0 = one ? 178 : 89;
    items[bit].level1 = 0;
    items[bit].duration1 = one ? 89 : 178;
  }
  rmt_write_items(kEscRmtChannel, items, 16, true);
}

void initializeEscSafeOutput() {
  rmt_config_t config = {};
  config.rmt_mode = RMT_MODE_TX;
  config.channel = kEscRmtChannel;
  config.gpio_num = static_cast<gpio_num_t>(kEscMotor1Pin);
  config.clk_div = 1;
  config.mem_block_num = 1;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  rmt_config(&config);
  rmt_driver_install(kEscRmtChannel, 0, 0);
  for (uint8_t index = 0; index < 100; ++index) {
    sendDshotPacket(0);
    delay(1);
  }
  Serial.printf("ESC M1 safe output: GPIO%u, DShot300\n", kEscMotor1Pin);
}

void sendDshotThrottle(uint16_t throttle) {
  for (uint16_t index = 0; index < 1000; ++index) {
    sendDshotPacket(throttle);
    delay(1);
  }
}

void handleEscTest() {
  const unsigned long nowMs = millis();

  if (escTestUntilMs == 0 && nowMs - lastDshotMs >= 1) {
    lastDshotMs = nowMs;
    sendDshotPacket(0);
  }

  if (Serial.available() > 0 && Serial.read() == 't') {
    sendDshotThrottle(kDshotThrottleTest);
    escTestUntilMs = nowMs;
    Serial.printf("ESC M1 test: DShot300 throttle %u\n", kDshotThrottleTest);
  }

  if (escTestUntilMs != 0) {
    sendDshotPacket(0);
    escTestUntilMs = 0;
    Serial.println("ESC M1 returned to zero");
  }
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
  display.printf("BAT: %.1f V\n", batteryNominalVoltage);
  display.printf("I2C: %s\n", i2cDevices.c_str());
  display.printf("MAG: %s\n", magnetometerModel);
  display.println("ESC: DSHOT SAFE");
  display.printf("M1: D%u 0", kEscMotor1Pin);
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
