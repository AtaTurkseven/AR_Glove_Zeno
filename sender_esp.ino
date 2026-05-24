/*
  AR_Glove_Zeno - Sender ESP32

  Hardware:
  - ESP32
  - MPU6050 on the glove/index finger

  Function:
  - Reads MPU6050 motion data
  - Estimates pitch / roll / yaw-ish orientation
  - Sends MotionPacket over ESP-NOW at ~60 Hz

  Target:
  - ESP32 Arduino Core 3.3.8
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

// =============================
// I2C pins for MPU6050
// =============================
#define I2C_SDA 21
#define I2C_SCL 22

// =============================
// ESP-NOW settings
// =============================
#define ESPNOW_CHANNEL 1

// Broadcast address: receiver does not need to be hardcoded.
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// =============================
// Send rate
// =============================
const unsigned long SEND_INTERVAL_US = 1000000UL / 60; // ~60 Hz

// =============================
// MPU6050
// =============================
Adafruit_MPU6050 mpu;

// =============================
// Packet format
// Must match receiver exactly
// =============================
typedef struct MotionPacket {
  uint32_t seq;
  uint32_t timestamp_ms;

  float ax;
  float ay;
  float az;

  float gx;
  float gy;
  float gz;

  float pitch;
  float roll;
  float yaw;
} MotionPacket;

MotionPacket packet;

// =============================
// State
// =============================
uint32_t seqCounter = 0;

unsigned long lastSendUs = 0;
unsigned long lastFilterUs = 0;

float pitch = 0.0f;
float roll  = 0.0f;
float yaw   = 0.0f;

// Gyro bias in deg/s
float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

// =============================
// ESP-NOW send callback
// ESP32 Arduino Core 3.3.x style
// =============================
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  // Keep this empty. It runs from the Wi-Fi task.
}

// =============================
// Helpers
// =============================
float radToDeg(float rad) {
  return rad * 57.2957795f;
}

void calibrateGyro() {
  Serial.println("Calibrating gyro. Hold glove still...");

  const int samples = 250;
  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumZ = 0.0f;

  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    sumX += radToDeg(g.gyro.x);
    sumY += radToDeg(g.gyro.y);
    sumZ += radToDeg(g.gyro.z);

    delay(5);
  }

  gyroBiasX = sumX / samples;
  gyroBiasY = sumY / samples;
  gyroBiasZ = sumZ / samples;

  Serial.print("Gyro bias X: ");
  Serial.print(gyroBiasX, 3);
  Serial.print(" Y: ");
  Serial.print(gyroBiasY, 3);
  Serial.print(" Z: ");
  Serial.println(gyroBiasZ, 3);

  // Initialize pitch/roll from accelerometer.
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  pitch = atan2(a.acceleration.y, sqrt(
                  a.acceleration.x * a.acceleration.x +
                  a.acceleration.z * a.acceleration.z
                )) * 57.2958f;

  roll = atan2(-a.acceleration.x, a.acceleration.z) * 57.2958f;

  yaw = 0.0f;
}

void updateOrientation(float ax, float ay, float az, float gxDeg, float gyDeg, float gzDeg, float dt) {
  // Accelerometer tilt estimate
  float accelPitch = atan2(ay, sqrt(ax * ax + az * az)) * 57.2958f;
  float accelRoll  = atan2(-ax, az) * 57.2958f;

  // Gyro integration
  pitch += gxDeg * dt;
  roll  += gyDeg * dt;
  yaw   += gzDeg * dt; // MPU6050 yaw drifts. This is only yaw-ish.

  // Complementary filter for pitch/roll
  const float alpha = 0.96f;
  pitch = alpha * pitch + (1.0f - alpha) * accelPitch;
  roll  = alpha * roll  + (1.0f - alpha) * accelRoll;
}

// =============================
// Setup MPU
// =============================
bool setupMPU() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("MPU6050 not found at 0x68.");
    return false;
  }

  Serial.println("MPU6050 found.");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  delay(500);
  calibrateGyro();

  return true;
}

// =============================
// Setup ESP-NOW
// =============================
bool setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);

  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("Sender MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed.");
    return false;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer.");
    return false;
  }

  Serial.println("ESP-NOW sender ready.");
  return true;
}

// =============================
// Arduino setup
// =============================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== AR_Glove_Zeno Sender ESP32 ===");

  if (!setupMPU()) {
    Serial.println("MPU setup failed. Stopped.");
    while (true) delay(1000);
  }

  if (!setupEspNow()) {
    Serial.println("ESP-NOW setup failed. Stopped.");
    while (true) delay(1000);
  }

  lastSendUs = micros();
  lastFilterUs = micros();

  Serial.println("Sender running.");
}

// =============================
// Arduino loop
// =============================
void loop() {
  unsigned long nowUs = micros();

  if (nowUs - lastSendUs < SEND_INTERVAL_US) {
    return;
  }

  lastSendUs = nowUs;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  unsigned long filterNowUs = micros();
  float dt = (filterNowUs - lastFilterUs) / 1000000.0f;
  lastFilterUs = filterNowUs;

  if (dt <= 0.0f || dt > 0.2f) {
    dt = 1.0f / 60.0f;
  }

  // Adafruit gyro is rad/s. Convert to deg/s and remove bias.
  float gxDeg = radToDeg(g.gyro.x) - gyroBiasX;
  float gyDeg = radToDeg(g.gyro.y) - gyroBiasY;
  float gzDeg = radToDeg(g.gyro.z) - gyroBiasZ;

  updateOrientation(
    a.acceleration.x,
    a.acceleration.y,
    a.acceleration.z,
    gxDeg,
    gyDeg,
    gzDeg,
    dt
  );

  packet.seq = seqCounter++;
  packet.timestamp_ms = millis();

  packet.ax = a.acceleration.x;
  packet.ay = a.acceleration.y;
  packet.az = a.acceleration.z;

  packet.gx = gxDeg;
  packet.gy = gyDeg;
  packet.gz = gzDeg;

  packet.pitch = pitch;
  packet.roll = roll;
  packet.yaw = yaw;

  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&packet, sizeof(packet));

  if (result != ESP_OK) {
    Serial.print("ESP-NOW send error: ");
    Serial.println(result);
  }

  if (packet.seq % 60 == 0) {
    Serial.print("SEQ ");
    Serial.print(packet.seq);
    Serial.print(" | P:");
    Serial.print(packet.pitch, 1);
    Serial.print(" R:");
    Serial.print(packet.roll, 1);
    Serial.print(" Y:");
    Serial.print(packet.yaw, 1);
    Serial.print(" | gx:");
    Serial.print(packet.gx, 1);
    Serial.print(" gz:");
    Serial.println(packet.gz, 1);
  }
}
