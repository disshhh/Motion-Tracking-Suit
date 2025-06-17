#include <WebSockets.h>
#include <WebSocketsServer.h>
#include <WebSocketsVersion.h>
#include <WiFi.h>
#include "Wire.h"
#include "MPU6050_6Axis_MotionApps612.h"

// WiFi credentials
const char* ssid = "Disha on the go!";
const char* password = "rishad007";

// WebSocket server
WebSocketsServer webSocket = WebSocketsServer(81);

// MPU6050
MPU6050 mpu68;
bool dmpReady = false;
uint8_t error_code = 0U;

// Timing
unsigned long lastSend = 0;
const unsigned long sendInterval = 15;
unsigned long recordCount = 0;

// Calibration
Quaternion qRef(0, 0, 0, 0);  // T-pose reference
Quaternion qSum(0, 0, 0, 0);  // Sum for averaging
bool calibrated = false;
bool collecting = false;
unsigned long calibrationStartTime = 0;
int calibrationSamples = 0;

// Quaternion multiplication helper
Quaternion multiplyQuaternions(Quaternion q1, Quaternion q2) {
  Quaternion result;
  result.w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z;
  result.x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y;
  result.y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x;
  result.z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w;
  return result;
}

void webSocketEvent(uint8_t client_num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("Client %u connected\n", client_num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("Client %u disconnected\n", client_num);
      break;
    case WStype_TEXT:
      Serial.printf("Received from %u: %s\n", client_num, payload);
      break;
  }
}

void setup() {
  Wire.begin(8, 9); // ESP32-C3 I2C pins
  Serial.begin(115200);

  // WiFi
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(10);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  // WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");

  // MPU6050 Initialization
  Serial.println("Initializing MPU6050...");
  mpu68.initialize();
  error_code = mpu68.dmpInitialize();

  if (error_code != 0) {
    Serial.println("MPU6050 DMP init failed");
    while (1);
  }

  if (!mpu68.testConnection()) {
    Serial.println("MPU6050 connection failed");
    while (1);
  }

  // STEP 1: STILL calibration
  Serial.println("==== STEP 1: STILL CALIBRATION ====");
  Serial.println("Place sensor flat and still (not worn).");
  delay(3000); // Give user 3s warning
  mpu68.CalibrateAccel(6);
  mpu68.CalibrateGyro(6);
  Serial.println("Still calibration complete.");

  // Enable DMP
  mpu68.setDMPEnabled(true);
  dmpReady = true;

  // STEP 2: T-POSE Calibration
  Serial.println("==== STEP 2: T-POSE CALIBRATION ====");
  Serial.println("Now wear the sensor and hold T-pose for 30 seconds...");
  delay(5000); // User preparation time
  collecting = true;
  calibrationStartTime = millis();
}

void loop() {
  webSocket.loop();
  if (!dmpReady) return;

  uint8_t fifo_buffer68[64];
  if (mpu68.dmpGetCurrentFIFOPacket(fifo_buffer68)) {
    Quaternion q68;
    mpu68.dmpGetQuaternion(&q68, fifo_buffer68);

    if (collecting) {
      unsigned long elapsed = millis() - calibrationStartTime;
      calibrationSamples++;
      qSum.w += q68.w;
      qSum.x += q68.x;
      qSum.y += q68.y;
      qSum.z += q68.z;

      if (elapsed % 5000 < 20) {
        Serial.print("T-pose calibration: ");
        Serial.print(30 - elapsed / 1000);
        Serial.println("s remaining...");
      }

      if (elapsed >= 30000) {
        collecting = false;
        calibrated = true;
        qRef.w = qSum.w / calibrationSamples;
        qRef.x = qSum.x / calibrationSamples;
        qRef.y = qSum.y / calibrationSamples;
        qRef.z = qSum.z / calibrationSamples;

        Serial.println("T-pose calibration done.");
        Serial.println("Streaming corrected quaternions...\n");
      }

      return; // Skip streaming until calibration is complete
    }

    if (calibrated && millis() - lastSend >= sendInterval) {
      lastSend = millis();
      recordCount++;

      Quaternion qRefConj(qRef.w, -qRef.x, -qRef.y, -qRef.z);  // Conjugate
      Quaternion qCorrected = multiplyQuaternions(qRefConj, q68);

      String sensorLabel = "RFA";
      String payload = "{\"count\": " + String(recordCount) +
                       ", \"label\": \"" + sensorLabel + "\", " +
                       "\"quaternion\": [" +
                       String(qCorrected.w, 4) + ", " +
                       String(qCorrected.x, 4) + ", " +
                       String(qCorrected.y, 4) + ", " +
                       String(qCorrected.z, 4) + "]}";

      Serial.println(payload);
      webSocket.broadcastTXT(payload);
    }
  }
}