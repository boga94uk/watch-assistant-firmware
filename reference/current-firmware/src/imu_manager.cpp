#include "imu_manager.h"

// QMI8658_L_SLAVE_ADDRESS is defined in SensorQMI8658.hpp

// Global sensor instance
static SensorQMI8658 qmi;
static IMUdata acc = {0, 0, 0};
static IMUdata gyr = {0, 0, 0};
static bool initialized = false;

namespace IMU {

bool init(TwoWire &wire, int sda, int scl) {
    Serial.println("🎯 Initializing IMU (QMI8658)...");
    
    if (qmi.begin(wire, QMI8658_L_SLAVE_ADDRESS, sda, scl)) {
        Serial.println("✅ QMI8658 initialized");
        
        // Configure accelerometer: ±4g range, 1000Hz sampling
        qmi.configAccelerometer(
            SensorQMI8658::ACC_RANGE_4G,
            SensorQMI8658::ACC_ODR_1000Hz
        );
        
        // Configure gyroscope: ±64dps range, ~900Hz sampling
        qmi.configGyroscope(
            SensorQMI8658::GYR_RANGE_64DPS,
            SensorQMI8658::GYR_ODR_896_8Hz
        );
        
        // Enable both sensors
        qmi.enableAccelerometer();
        qmi.enableGyroscope();
        
        initialized = true;
        Serial.println("   Accelerometer: ±4g, 1000Hz");
        Serial.println("   Gyroscope: ±64dps, 896Hz");
        
        return true;
    } else {
        Serial.println("❌ QMI8658 initialization failed!");
        initialized = false;
        return false;
    }
}

bool isReady() {
    return initialized;
}

bool update() {
    if (!initialized) return false;
    
    if (qmi.getDataReady()) {
        qmi.getAccelerometer(acc.x, acc.y, acc.z);
        qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
        return true;
    }
    
    return false;
}

bool getAccelerometer(IMUdata &data) {
    if (!initialized) return false;
    
    data = acc;
    return true;
}

bool getGyroscope(IMUdata &data) {
    if (!initialized) return false;
    
    data = gyr;
    return true;
}

float getAccelerometerMagnitude() {
    if (!initialized) return 0.0f;
    
    return sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
}

bool detectShake(float threshold) {
    if (!initialized) return false;
    
    float magnitude = getAccelerometerMagnitude();
    
    // Shake detected if total acceleration > threshold
    // (normal gravity is 1g, so >2g indicates movement)
    return (magnitude > threshold);
}

int getOrientation() {
    if (!initialized) return 0;
    
    // Determine orientation based on which axis has strongest gravity
    float abs_x = abs(acc.x);
    float abs_y = abs(acc.y);
    float abs_z = abs(acc.z);
    
    // Device is flat if Z-axis dominates
    if (abs_z > abs_x && abs_z > abs_y) {
        return 0;  // Flat (face up or down)
    }
    
    // Portrait/landscape based on X/Y dominance
    if (abs_y > abs_x) {
        return acc.y > 0 ? 1 : 4;  // 1=portrait, 4=upside_down
    } else {
        return acc.x > 0 ? 2 : 3;  // 2=landscape_left, 3=landscape_right
    }
}

void printValues() {
    if (!initialized) {
        Serial.println("IMU not initialized");
        return;
    }
    
    Serial.printf("Accelerometer (g):  X=%.2f  Y=%.2f  Z=%.2f  |  Mag=%.2f\n",
                  acc.x, acc.y, acc.z, getAccelerometerMagnitude());
    Serial.printf("Gyroscope (dps):    X=%.1f  Y=%.1f  Z=%.1f\n",
                  gyr.x, gyr.y, gyr.z);
}

} // namespace IMU
