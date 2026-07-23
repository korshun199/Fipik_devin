#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {

constexpr char kConfigPath[] = "/flight-controller.json";

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

  if (nominalVoltage <= 0.0F || capacityMah == 0) {
    Serial.println("Battery configuration is incomplete.");
    return false;
  }

  Serial.printf("Vehicle: %s\n", config["vehicle"]["name"] | "unnamed");
  Serial.printf("Battery: %.1f V, %lu mAh\n", nominalVoltage, capacityMah);
  Serial.printf("ESC: %s\n", config["esc"]["model"] | "unknown");
  Serial.printf("Motor output: %s\n", motorOutputEnabled ? "enabled" : "disabled");

  if (motorOutputEnabled) {
    Serial.println("Motor output is not implemented in this prototype.");
  }

  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
    return;
  }

  loadConfiguration();
}

void loop() {
  delay(1000);
}
