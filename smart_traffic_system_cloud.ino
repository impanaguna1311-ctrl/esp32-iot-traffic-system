#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// ===================== BLYNK CONFIGURATION =====================
#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Smart Traffic System"
#define BLYNK_AUTH_TOKEN "YOUR_AUTH_TOKEN"

char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// ===================== OLED =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===================== RFID =====================
#define RFID_SS 5
#define RFID_RST 4
MFRC522 mfrc522(RFID_SS, RFID_RST);

// ===================== IR Sensors =====================
const int IR_PINS[3] = {34, 35, 39};

// ===================== Traffic Lights =====================
const int RED_PINS[3] = {13, 17, 27};
const int YELLOW_PINS[3] = {14, 25, 32};
const int GREEN_PINS[3] = {16, 26, 33};

const int LANE_COUNT = 3;

// ===================== RFID Tag UID List =====================
const String AUTHORIZED_TAGS[3] = {
  "XX XX XX XX",  // Tag 1 -> Lane 1
  "YY YY YY YY",  // Tag 2 -> Lane 2
  "ZZ ZZ ZZ ZZ"   // Tag 3 -> Lane 3
};

// ===================== System Variables =====================
int activeLane = 0;
int priorityLane = -1;
bool laneVehicle[3] = {false, false, false};
int vehicleCount[3] = {0, 0, 0};
String lastRFID = "No tag";
String statusMessage = "System Ready";
String lastTagOwner = "No access";

unsigned long previousCycleTime = 0;
const unsigned long laneCycleTime = 8000;
unsigned long previousRfidScanTime = 0;
const unsigned long rfidDebounce = 1500;
unsigned long lastBlynkUpdate = 0;
const unsigned long blynkUpdateInterval = 1000; // Update Blynk every 1 second

// ===================== BLYNK VIRTUAL PINS =====================
// V0 = Lane 1 Status (Green=1, Red=0)
// V1 = Lane 2 Status (Green=1, Red=0)
// V2 = Lane 3 Status (Green=1, Red=0)
// V3 = Lane 1 Vehicle Count
// V4 = Lane 2 Vehicle Count
// V5 = Lane 3 Vehicle Count
// V6 = Current Active Lane (1, 2, or 3)
// V7 = RFID Tag UID
// V8 = Authorization Status
// V9 = System Status Message

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\nESP32 Smart Traffic + RFID + Blynk Cloud System");
  Serial.println("Initializing...");

  // Initialize I2C OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Smart Traffic System");
  display.println("Connecting to WiFi...");
  display.display();
  delay(1500);

  // Initialize SPI RFID
  SPI.begin(18, 19, 23, RFID_SS);
  mfrc522.PCD_Init();
  Serial.println("RFID Module Initialized");

  // Traffic lights setup
  for (int i = 0; i < LANE_COUNT; i++) {
    pinMode(RED_PINS[i], OUTPUT);
    pinMode(YELLOW_PINS[i], OUTPUT);
    pinMode(GREEN_PINS[i], OUTPUT);

    digitalWrite(RED_PINS[i], HIGH);
    digitalWrite(YELLOW_PINS[i], HIGH);
    digitalWrite(GREEN_PINS[i], HIGH);
  }
  Serial.println("Traffic Lights Initialized");

  // IR sensors setup
  for (int i = 0; i < LANE_COUNT; i++) {
    pinMode(IR_PINS[i], INPUT);
  }
  Serial.println("IR Sensors Initialized");

  setAllRed();
  activeLane = 0;
  updateTrafficState();

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    displayMessage("WiFi Connected", WiFi.localIP().toString());
    delay(2000);

    // Connect to Blynk
    Serial.println("Connecting to Blynk...");
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect();
  } else {
    Serial.println("\nWiFi Connection Failed!");
    displayMessage("WiFi Failed", "Offline Mode");
    delay(2000);
  }

  displayMessage("System Ready", "Scan RFID Tag");
  delay(1000);
  
  Serial.println("Setup Complete!");
}

void loop() {
  // Blynk connection handler
  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected()) {
      Blynk.run();
    } else {
      Blynk.connect();
    }
  }

  updateIRStatus();
  readRFID();

  // Priority lane logic
  if (priorityLane != -1) {
    activeLane = priorityLane;
    updateTrafficState();

    if (millis() - previousCycleTime > laneCycleTime) {
      priorityLane = -1;
      previousCycleTime = millis();
      statusMessage = "Normal Mode";
    }
  } else {
    // Normal lane selection
    int detectedLane = -1;
    for (int i = 0; i < LANE_COUNT; i++) {
      if (laneVehicle[i]) {
        detectedLane = i;
        break;
      }
    }

    if (detectedLane != -1) {
      activeLane = detectedLane;
      updateTrafficState();
      previousCycleTime = millis();
    } else {
      if (millis() - previousCycleTime > laneCycleTime) {
        activeLane = (activeLane + 1) % LANE_COUNT;
        updateTrafficState();
        previousCycleTime = millis();
      }
    }
  }

  updateOLED();
  
  // Update Blynk with current system state
  if (millis() - lastBlynkUpdate > blynkUpdateInterval && WiFi.status() == WL_CONNECTED) {
    lastBlynkUpdate = millis();
    updateBlynkCloud();
  }

  delay(200);
}

// ===================== OLED FUNCTIONS =====================
void displayMessage(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.setCursor(0, 20);
  display.println(line2);
  display.display();
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Line 0: Active Lane
  display.setCursor(0, 0);
  display.print("Lane: ");
  display.println(activeLane + 1);

  // Line 1: IR Sensor Status
  display.setCursor(0, 10);
  display.print("IR: ");
  for (int i = 0; i < 3; i++) {
    display.print(laneVehicle[i] ? "1" : "0");
    if (i < 2) display.print(" ");
  }
  display.println("");

  // Line 2: Vehicle Count
  display.setCursor(0, 20);
  display.print("Count: ");
  for (int i = 0; i < 3; i++) {
    display.print(vehicleCount[i]);
    if (i < 2) display.print(",");
  }
  display.println("");

  // Line 3: RFID Info
  display.setCursor(0, 30);
  display.print("RFID: ");
  display.println(lastRFID.substring(0, 11));

  // Line 4: Status
  display.setCursor(0, 40);
  display.println(statusMessage);

  // Line 5: WiFi Status
  display.setCursor(0, 50);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: ");
    display.println(Blynk.connected() ? "Online" : "Connecting");
  } else {
    display.println("WiFi: Offline");
  }

  display.display();
}

// ===================== IR FUNCTIONS =====================
void updateIRStatus() {
  for (int i = 0; i < LANE_COUNT; i++) {
    int value = digitalRead(IR_PINS[i]);
    bool previousState = laneVehicle[i];
    
    // IR sensor outputs LOW when object is detected
    laneVehicle[i] = (value == LOW);

    // Count vehicles when transition from no vehicle to vehicle
    if (!previousState && laneVehicle[i]) {
      vehicleCount[i]++;
      Serial.println("Vehicle detected in Lane " + String(i + 1) + " - Count: " + String(vehicleCount[i]));
    }
  }
}

// ===================== TRAFFIC LIGHT FUNCTIONS =====================
void setAllRed() {
  for (int i = 0; i < LANE_COUNT; i++) {
    digitalWrite(RED_PINS[i], LOW);    // Red ON
    digitalWrite(YELLOW_PINS[i], HIGH); // Yellow OFF
    digitalWrite(GREEN_PINS[i], HIGH);  // Green OFF
  }
}

void updateTrafficState() {
  for (int i = 0; i < LANE_COUNT; i++) {
    if (i == activeLane) {
      // Active lane: GREEN
      digitalWrite(RED_PINS[i], HIGH);
      digitalWrite(YELLOW_PINS[i], HIGH);
      digitalWrite(GREEN_PINS[i], LOW);
    } else {
      // Inactive lanes: RED
      digitalWrite(RED_PINS[i], LOW);
      digitalWrite(YELLOW_PINS[i], HIGH);
      digitalWrite(GREEN_PINS[i], HIGH);
    }
  }
}

// ===================== RFID FUNCTIONS =====================
void readRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  if (millis() - previousRfidScanTime < rfidDebounce) {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  previousRfidScanTime = millis();

  lastRFID = printUID(mfrc522.uid);
  Serial.println("RFID Tag scanned: " + lastRFID);

  int matchedLane = -1;
  for (int i = 0; i < 3; i++) {
    if (lastRFID == AUTHORIZED_TAGS[i]) {
      matchedLane = i;
      break;
    }
  }

  if (matchedLane != -1) {
    priorityLane = matchedLane;
    activeLane = matchedLane;
    statusMessage = "Authorized";
    lastTagOwner = "Lane " + String(matchedLane + 1) + " priority";
    previousCycleTime = millis();
    updateTrafficState();
    Serial.println("Authorization granted for Lane " + String(matchedLane + 1));
  } else {
    statusMessage = "Access Denied";
    lastTagOwner = "Unauthorized Tag";
    Serial.println("Unauthorized RFID tag scanned");
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

String printUID(MFRC522::Uid uid) {
  String result = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) result += "0";
    result += String(uid.uidByte[i], HEX);
    if (i != uid.size - 1) result += " ";
  }
  result.toUpperCase();
  return result;
}

// ===================== BLYNK CLOUD UPDATE FUNCTIONS =====================
void updateBlynkCloud() {
  if (!Blynk.connected()) {
    return;
  }

  // Send Lane Status (Green=1, Red=0)
  for (int i = 0; i < 3; i++) {
    bool isGreen = (i == activeLane) ? 1 : 0;
    Blynk.virtualWrite(V0 + i, isGreen);
  }

  // Send Vehicle Counts
  Blynk.virtualWrite(V3, vehicleCount[0]);
  Blynk.virtualWrite(V4, vehicleCount[1]);
  Blynk.virtualWrite(V5, vehicleCount[2]);

  // Send Active Lane
  Blynk.virtualWrite(V6, activeLane + 1);

  // Send RFID UID
  Blynk.virtualWrite(V7, lastRFID);

  // Send Authorization Status
  Blynk.virtualWrite(V8, lastTagOwner);

  // Send System Status
  Blynk.virtualWrite(V9, statusMessage);

  Serial.println("Blynk Updated: Lane " + String(activeLane + 1) + " | RFID: " + lastRFID);
}

// ===================== BLYNK VIRTUAL PIN HANDLERS =====================
BLYNK_WRITE(V10) {
  // Manual Lane Selection from Blynk App
  int selectedLane = param.asInt() - 1;
  if (selectedLane >= 0 && selectedLane < 3) {
    activeLane = selectedLane;
    priorityLane = selectedLane;
    statusMessage = "Manual Override";
    updateTrafficState();
    previousCycleTime = millis();
    Serial.println("Manual override: Lane " + String(activeLane + 1) + " selected from Blynk");
  }
}

BLYNK_WRITE(V11) {
  // Reset vehicle counts from Blynk App
  if (param.asInt() == 1) {
    vehicleCount[0] = 0;
    vehicleCount[1] = 0;
    vehicleCount[2] = 0;
    Serial.println("Vehicle counts reset from Blynk");
  }
}

BLYNK_CONNECTED() {
  Serial.println("Blynk Connected!");
  displayMessage("Blynk Connected", "Cloud Sync Active");
  delay(1000);
  updateBlynkCloud(); // Initial sync
}
