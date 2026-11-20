#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SensorQMI8658.hpp>

// ============================================
// IMU MANAGER - QMI8658 6-Axis Sensor
// ============================================
// Wrapper for accelerometer and gyroscope data
// Note: IMUdata is defined in SensorQMI8658.hpp

namespace IMU {

/**
 * Initialize QMI8658 IMU sensor
 * @param wire I2C interface
 * @param sda SDA pin
 * @param scl SCL pin
 * @return true if initialization successful
 */
bool init(TwoWire &wire = Wire, int sda = 15, int scl = 14);

/**
 * Check if IMU is initialized and working
 * @return true if IMU is ready
 */
bool isReady();

/**
 * Update sensor readings (call regularly in loop)
 * @return true if new data available
 */
bool update();

/**
 * Get latest accelerometer data
 * @param data Output structure for x, y, z values (in g)
 * @return true if data is valid
 */
bool getAccelerometer(IMUdata &data);

/**
 * Get latest gyroscope data
 * @param data Output structure for x, y, z values (in dps)
 * @return true if data is valid
 */
bool getGyroscope(IMUdata &data);

/**
 * Get accelerometer magnitude (total acceleration)
 * @return Magnitude in g
 */
float getAccelerometerMagnitude();

/**
 * Detect if device is being shaken
 * @param threshold Acceleration threshold (default 2.0g)
 * @return true if shake detected
 */
bool detectShake(float threshold = 2.0f);

/**
 * Detect device orientation
 * @return 0=flat, 1=portrait, 2=landscape_left, 3=landscape_right, 4=upside_down
 */
int getOrientation();

/**
 * Print current sensor values to serial (for debugging)
 */
void printValues();

} // namespace IMU
