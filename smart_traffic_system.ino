#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// RFID Configuration
#define RFID_SS 5
#define RFID_RST 4
MFRC522 mfrc522(RFID_SS, RFID_RST);

// IR Sensor Pins
const int IR_PINS[3] = {34, 35, 39};

// Traffic Module Pins
// Lane 1: Red = 13, Yellow = 14, Green = 16
// Lane 2: Red = 17, Yellow = 25, Green = 26
// Lane 3: Red = 27, Yellow = 32, Green = 33

const int RED_PINS[3] = {13, 17, 27};
const int YELLOW_PINS[3] = {14, 25, 32};
const int GREEN_PINS[3] = {16, 26, 33};

const int LANE_COUNT = 3;

// RFID Configuration
const String AUTHORIZED_TAGS[3] = {
  "A1 B2 C3 D4",  // Lane 1 - Replace with your tag UID
  "11 22 33 44",  // Lane 2 - Replace with your tag UID
  "99 AA BB CC"   // Lane 3 - Replace with your tag UID
};

// System Variables
int activeLane = 0;
int priorityLane = -1;
bool laneVehicle[3] = {false, false, false};
int vehicleCount[3] = {0, 0, 0};
String lastRFID = "No tag";
String statusMessage = "System Ready";
String lastTagOwner = "No access";

unsigned long previousCycleTime = 0;
const unsigned long laneCycleTime = 8000;  // 8 seconds per lane
unsigned long previousRfidScanTime = 0;
const unsigned long rfidDebounce = 1500;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nSystem Starting...");

  // Initialize I2C for OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("IoT Traffic System");
  display.println("Initializing...");
  display.display();
  delay(2000);

  // Initialize SPI for RFID
  SPI.begin(18, 19, 23, RFID_SS);
  mfrc522.PCD_Init();
  Serial.println("RFID Module Initialized");

  // Initialize Traffic Light Pins
  for (int i = 0; i < LANE_COUNT; i++) {
    pinMode(RED_PINS[i], OUTPUT);
    pinMode(YELLOW_PINS[i], OUTPUT);
    pinMode(GREEN_PINS[i], OUTPUT);
    
    // Turn off all lights initially
    digitalWrite(RED_PINS[i], HIGH);
    digitalWrite(YELLOW_PINS[i], HIGH);
    digitalWrite(GREEN_PINS[i], HIGH);
  }
  Serial.println("Traffic Lights Initialized");

  // Initialize IR Sensor Pins
  for (int i = 0; i < LANE_COUNT; i++) {
    pinMode(IR_PINS[i], INPUT);
  }
  Serial.println("IR Sensors Initialized");

  // Set initial state
  setAllRed();
  activeLane = 0;
  updateTrafficState();
  
  displayMessage("System Ready", "Waiting for cars");
  delay(2000);
  
  Serial.println("Setup Complete!");
}

void loop() {
  updateIRStatus();
  readRFID();

  // Priority Lane Logic
  if (priorityLane != -1) {
    activeLane = priorityLane;
    updateTrafficState();
    
    if (millis() - previousCycleTime > laneCycleTime) {
      priorityLane = -1;
      previousCycleTime = millis();
      Serial.println("Priority mode ended");
    }
  } else {
    // Normal operation - find first lane with vehicle
    int detectedLane = -1;
    for (int i = 0; i < LANE_COUNT; i++) {
      if (laneVehicle[i]) {
        detectedLane = i;
        break;
      }
    }

    if (detectedLane != -1 && detectedLane != activeLane) {
      activeLane = detectedLane;
      updateTrafficState();
      previousCycleTime = millis();
    }

    // Cycle through lanes if no vehicle detected or timer expired
    if (millis() - previousCycleTime > laneCycleTime) {
      previousCycleTime = millis();
      
      // Find next lane with vehicle
      int nextLane = (activeLane + 1) % LANE_COUNT;
      int attempts = 0;
      
      while (!laneVehicle[nextLane] && attempts < LANE_COUNT) {
        nextLane = (nextLane + 1) % LANE_COUNT;
        attempts++;
      }
      
      activeLane = nextLane;
      updateTrafficState();
      Serial.println("Cycling to Lane " + String(activeLane + 1));
    }
  }

  updateOLED();
  delay(200);
}

void initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    while (true);
  }
  display.clearDisplay();
  display.display();
}

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
  
  // Line 1: Active Lane
  display.setCursor(0, 0);
  display.print("Active Lane: ");
  display.println(activeLane + 1);
  
  // Line 2: IR Sensor Status
  display.setCursor(0, 10);
  display.print("IR Status: ");
  for (int i = 0; i < LANE_COUNT; i++) {
    display.print(laneVehicle[i] ? "1" : "0");
    if (i < 2) display.print(",");
  }
  display.println("");
  
  // Line 3: Vehicle Count
  display.setCursor(0, 20);
  display.print("Count: ");
  for (int i = 0; i < LANE_COUNT; i++) {
    display.print(vehicleCount[i]);
    if (i < 2) display.print(",");
  }
  display.println("");
  
  // Line 4: RFID Info
  display.setCursor(0, 30);
  display.print("RFID: ");
  display.println(lastRFID.substring(0, 11));
  
  // Line 5: Status Message
  display.setCursor(0, 40);
  display.println(statusMessage);
  
  // Line 6: Tag Owner
  display.setCursor(0, 50);
  display.println(lastTagOwner);
  
  display.display();
}

void updateIRStatus() {
  for (int i = 0; i < LANE_COUNT; i++) {
    int value = digitalRead(IR_PINS[i]);
    bool previousState = laneVehicle[i];
    laneVehicle[i] = (value == LOW);  // LOW means object detected
    
    // Count vehicles (increment when transition from no vehicle to vehicle)
    if (!previousState && laneVehicle[i]) {
      vehicleCount[i]++;
      Serial.println("Vehicle detected in Lane " + String(i + 1) + " - Count: " + String(vehicleCount[i]));
    }
  }
}

void setAllRed() {
  for (int i = 0; i < LANE_COUNT; i++) {
    digitalWrite(RED_PINS[i], LOW);    // Red ON
    digitalWrite(YELLOW_PINS[i], HIGH); // Yellow OFF
    digitalWrite(GREEN_PINS[i], HIGH);  // Green OFF
  }
  Serial.println("All lanes set to RED");
}

void updateTrafficState() {
  for (int i = 0; i < LANE_COUNT; i++) {
    if (i == activeLane) {
      // Active lane: GREEN
      digitalWrite(RED_PINS[i], HIGH);   // Red OFF
      digitalWrite(YELLOW_PINS[i], HIGH); // Yellow OFF
      digitalWrite(GREEN_PINS[i], LOW);  // Green ON
      Serial.println("Lane " + String(i + 1) + " - GREEN");
    } else {
      // Inactive lanes: RED
      digitalWrite(RED_PINS[i], LOW);    // Red ON
      digitalWrite(YELLOW_PINS[i], HIGH); // Yellow OFF
      digitalWrite(GREEN_PINS[i], HIGH);  // Green OFF
      Serial.println("Lane " + String(i + 1) + " - RED");
    }
  }
}

void readRFID() {
  // Check if a new RFID card is present
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Try to read the card serial number
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Debounce RFID reads
  if (millis() - previousRfidScanTime < rfidDebounce) {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  previousRfidScanTime = millis();
  
  // Get the UID as a string
  lastRFID = printUID(mfrc522.uid);
  Serial.println("RFID Tag detected: " + lastRFID);

  // Check if the tag is authorized
  int matchedLane = -1;
  for (int i = 0; i < 3; i++) {
    if (lastRFID == AUTHORIZED_TAGS[i]) {
      matchedLane = i;
      break;
    }
  }

  if (matchedLane != -1) {
    // Authorized tag - give priority to this lane
    priorityLane = matchedLane;
    activeLane = matchedLane;
    statusMessage = "Authorized!";
    lastTagOwner = "Lane " + String(matchedLane + 1) + " Priority";
    previousCycleTime = millis();
    updateTrafficState();
    Serial.println("RFID Authorization: Lane " + String(matchedLane + 1) + " gets priority");
  } else {
    // Unauthorized tag
    statusMessage = "Access Denied!";
    lastTagOwner = "Unauthorized Tag";
    Serial.println("RFID: Unauthorized tag detected");
  }

  // Halt the RFID reader
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

String printUID(MFRC522::Uid uid) {
  String result = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) {
      result += "0";
    }
    result += String(uid.uidByte[i], HEX);
    if (i != uid.size - 1) {
      result += " ";
    }
  }
  result.toUpperCase();
  return result;
}
