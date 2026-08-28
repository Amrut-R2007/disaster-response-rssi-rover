#include <esp_now.h>
#include <WiFi.h>

// Disable ESP32 Brownout Detector to prevent reset on motor inrush current
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- L298N PIN CONNECTIONS ---
const int PIN_IN1 = 25; // Left Motor Forward
const int PIN_IN2 = 26; // Left Motor Reverse
const int PIN_IN3 = 27; // Right Motor Forward
const int PIN_IN4 = 14; // Right Motor Reverse

// --- PACKET DATA STRUCTURES ---
typedef struct struct_control {
  int xValue;
  int yValue;
  bool buttonVal;
} struct_control;

typedef struct struct_telemetry {
  int rssi; // Telemetry RSSI sent back to remote
} struct_telemetry;

struct_control incomingData;
struct_telemetry outgoingTelemetry;

// Failsafe timer to stop motors if remote disconnects
unsigned long lastPacketTime = 0;
const unsigned long FAILSAFE_TIMEOUT_MS = 350;

// Motor Control Helper
void driveMotors(int in1, int in2, int in3, int in4) {
  digitalWrite(PIN_IN1, in1);
  digitalWrite(PIN_IN2, in2);
  digitalWrite(PIN_IN3, in3);
  digitalWrite(PIN_IN4, in4);
}

// Receive Callback: Processes drive commands & returns RSSI
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));
  lastPacketTime = millis(); // Refresh watchdog timer

  // 1. Capture packet RSSI
  if (info->rx_ctrl != NULL) {
    outgoingTelemetry.rssi = info->rx_ctrl->rssi;
  }

  // 2. Dynamically register Transmitter MAC address as peer on first contact
  if (!esp_now_is_peer_exist(info->src_addr)) {
    esp_now_peer_info_t autoPeer = {};
    memcpy(autoPeer.peer_addr, info->src_addr, 6);
    autoPeer.channel = 1;
    autoPeer.encrypt = false;
    esp_now_add_peer(&autoPeer);
    Serial.println("[SYSTEM] Remote paired dynamically!");
  }

  // 3. Directional Drive Logic (Deadzone: 1500 to 2700)
  int x = incomingData.xValue;
  int y = incomingData.yValue;

  if (y < 1500) {
    driveMotors(HIGH, LOW, HIGH, LOW); // Forward
  } 
  else if (y > 2700) {
    driveMotors(LOW, HIGH, LOW, HIGH); // Backward
  } 
  else if (x < 1500) {
    driveMotors(LOW, HIGH, HIGH, LOW); // Spin Left
  } 
  else if (x > 2700) {
    driveMotors(HIGH, LOW, LOW, HIGH); // Spin Right
  } 
  else {
    driveMotors(LOW, LOW, LOW, LOW);   // Joystick in neutral center
  }

  // 4. Send RSSI telemetry back to transmitter
  esp_now_send(info->src_addr, (uint8_t *)&outgoingTelemetry, sizeof(outgoingTelemetry));
}

void setup() {
  // CRITICAL: Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // Motor Pin Setup
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  driveMotors(LOW, LOW, LOW, LOW);

  // Set Wi-Fi to Station Mode & lock to Channel 1
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW initialization failed!");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("[SYSTEM] Rover active. Waiting for Transmitter packets...");
}

void loop() {
  // Safety Watchdog: Shut down motors if transmitter signal is lost
  if (millis() - lastPacketTime > FAILSAFE_TIMEOUT_MS) {
    driveMotors(LOW, LOW, LOW, LOW);
  }
}