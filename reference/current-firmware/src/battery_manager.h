#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

// Define chip type before including XPowersLib
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

// Battery state
extern bool batteryInitialized;
extern XPowersPMU power;

// Function declarations
bool initBattery();
String getBatteryStatus();
float getBatteryVoltage();
int getBatteryPercent();
bool isCharging();
bool isVbusConnected();

#endif // BATTERY_MANAGER_H
