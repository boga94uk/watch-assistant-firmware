// ============================================
// BATTERY MANAGER - Implementation
// ============================================

#include "battery_manager.h"
#include "hardware_pins.h"

// Battery state
bool batteryInitialized = false;
XPowersPMU power;

bool initBattery() {
  // Initialize PMU
  if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    batteryInitialized = false;
    return false;
  }
  
  // Enable ADC measurements
  power.enableTemperatureMeasure();
  power.enableBattDetection();
  power.enableVbusVoltageMeasure();
  power.enableBattVoltageMeasure();
  power.enableSystemVoltageMeasure();
  
  Serial.println("✅ AXP2101 initialized");
  Serial.printf("   Battery: %s\n", power.isBatteryConnect() ? "Connected" : "Not detected");
  Serial.printf("   VBUS: %s\n", power.isVbusIn() ? "Connected" : "Not connected");
  
  batteryInitialized = true;
  return true;
}

String getBatteryStatus() {
  if (!batteryInitialized) {
    return "Battery: N/A";
  }
  
  String status = "";
  
  // Battery voltage and percentage
  if (power.isBatteryConnect()) {
    float voltage = power.getBattVoltage() / 1000.0;  // Convert mV to V
    int percent = power.getBatteryPercent();
    status += String(percent) + "% (" + String(voltage, 2) + "V)";
  } else {
    status += "No Battery";
  }
  
  // Charging status
  if (power.isCharging()) {
    status += " [Charging]";
  } else if (power.isVbusIn()) {
    status += " [Plugged]";
  }
  
  return status;
}

float getBatteryVoltage() {
  if (!batteryInitialized || !power.isBatteryConnect()) {
    return 0.0;
  }
  return power.getBattVoltage() / 1000.0;  // Convert mV to V
}

int getBatteryPercent() {
  if (!batteryInitialized || !power.isBatteryConnect()) {
    return -1;
  }
  return power.getBatteryPercent();
}

bool isCharging() {
  if (!batteryInitialized) {
    return false;
  }
  return power.isCharging();
}

bool isVbusConnected() {
  if (!batteryInitialized) {
    return false;
  }
  return power.isVbusIn();
}
