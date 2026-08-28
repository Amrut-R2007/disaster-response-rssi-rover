#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Initialize 16x2 LCD at I2C address 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- PIN DEFINITIONS ---
const int PIN_JOY_X  = 34; // Steering
const int PIN_JOY_Y  = 35; // Throttle
const int PIN_JOY_SW = 32; // Joystick Push Button

// --- ROVER MAC ADDRESS ---
uint8_t roverAddress[] = {0x7C, 0x9E, 0xBD, 0x60, 0x7E, 0x30};

// --- DATA PACKET STRUCTURES ---
typedef struct struct_control {
  int xValue;
  int yValue;
  bool buttonVal;
} struct_control;

typedef struct struct_telemetry {
  int rssi;
} struct_telemetry;

struct_control controlData;
struct_telemetry incomingTelemetry;
esp_now_peer_info_t peerInfo;

int liveRSSI = -100;

// Telemetry Receive Callback (Captures live RSSI from Rover)
void OnTelemetryRecv(const esp_now_recv_info *info, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingTelemetry, incomingDataPtr, sizeof(incomingTelemetry));
  liveRSSI = incomingTelemetry.rssi;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);

  // Stabilize I2C bus timing for PCF8574 chip
  Wire.begin(21, 22);
  Wire.setClock(50000); 

  // LCD Setup
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Survivor Finder");
  lcd.setCursor(0, 1);
  lcd.print("Linking...");

  // ESP-NOW Radio Setup
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1, WIFI_SECOND_CHAN_NONE); // Locked to Channel 1

  if (esp_now_init() != ESP_OK) {
    lcd.setCursor(0, 1);
    lcd.print("ESP-NOW Fail!  ");
    return;
  }

  // Register Rover Peer
  memcpy(peerInfo.peer_addr, roverAddress, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    lcd.setCursor(0, 1);
    lcd.print("Peer Fail!     ");
    return;
  }

  esp_now_register_recv_cb(OnTelemetryRecv);

  delay(1000);
  lcd.clear();
}

void loop() {
  // 1. Read Analog Joystick
  controlData.xValue = analogRead(PIN_JOY_X);
  controlData.yValue = analogRead(PIN_JOY_Y);
  controlData.buttonVal = digitalRead(PIN_JOY_SW);

  // 2. Transmit Control Packet to Rover
  esp_now_send(roverAddress, (uint8_t *)&controlData, sizeof(controlData));

  // 3. LCD Line 1: Formatted coordinates (cleans ghost digits)
  char line0[17];
  snprintf(line0, sizeof(line0), "X:%-4d  Y:%-4d", controlData.xValue / 100, controlData.yValue / 100);
  lcd.setCursor(0, 0);
  lcd.print(line0);

  // 4. LCD Line 2: Signal Strength & Proximity Zone
  char line1[17];
  if (liveRSSI > -55 && liveRSSI != -100) {
    snprintf(line1, sizeof(line1), "Sig:%4ddBm HOT ", liveRSSI);
  } else if (liveRSSI > -75 && liveRSSI != -100) {
    snprintf(line1, sizeof(line1), "Sig:%4ddBm WARM", liveRSSI);
  } else {
    snprintf(line1, sizeof(line1), "Sig:%4ddBm COLD", liveRSSI);
  }
  lcd.setCursor(0, 1);
  lcd.print(line1);

  delay(50); // 20Hz update cycle
}