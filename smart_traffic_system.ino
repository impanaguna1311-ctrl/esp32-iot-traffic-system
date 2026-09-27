#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define SS_PIN 5
#define RST_PIN 4
MFRC522 mfrc522(SS_PIN, RST_PIN);

#define BLYNK_ENABLE 0

#if BLYNK_ENABLE
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

char auth[] = "YOUR_BLYNK_AUTH_TOKEN";
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";
#endif

const int IR_PINS[3] = {34, 35, 36};
const int RED_PINS[3] = {25, 32, 15};
const int YELLOW_PINS[3] = {26, 33, 2};
const int GREEN_PINS[3] = {27, 14, 13};

const bool IR_ACTIVE_LOW = true;

const String AUTHORIZED_TAGS[3] = {
  "A1 B2 C3 D4",  // Example UID 1
  "11 22 33 44",  // Example UID 2
  "99 AA BB CC"   // Example UID 3
};

const int LANE_COUNT = 3;
int activeLane = 0;
int priorityLane = -1;
bool laneVehicle[3] = {false, false, false};
String lastRFID = "No tag";
String statusMessage = "System Ready";
String lastTagOwner = "No access";

unsigned long previousCycleTime = 0;
const unsigned long laneCycleTime = 8000;
unsigned long previousRfidScanTime = 0;
const unsigned long rfidDebounce = 1500;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  for (int i = 0; i < 3; i++) {
    pinMode(RED_PINS[i], OUTPUT);
    pinMode(YELLOW_PINS[i], OUTPUT);
    pinMode(GREEN_PINS[i], OUTPUT);
    pinMode(IR_PINS[i], INPUT);
    digitalWrite(RED_PINS[i], HIGH);
    digitalWrite(YELLOW_PINS[i], HIGH);
    digitalWrite(GREEN_PINS[i], HIGH);
  }

  initOLED();
  displayMessage("Initializing...", "Waiting for cars");
  delay(2000);

#if BLYNK_ENABLE
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Blynk.begin(auth, ssid, pass);
#endif

  setAllRed();
  activeLane = 0;
  updateTrafficState();
  displayMessage("System Ready", "Scan RFID tag");
}

void loop() {
#if BLYNK_ENABLE
  Blynk.run();
#endif

  updateIRStatus();
  readRFID();

  if (priorityLane != -1) {
    activeLane = priorityLane;
    updateTrafficState();
    if (millis() - previousCycleTime > laneCycleTime) {
      priorityLane = -1;
      previousCycleTime = millis();
    }
  } else {
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
    } else {
      activeLane = 0;
      updateTrafficState();
    }

    if (millis() - previousCycleTime > laneCycleTime) {
      previousCycleTime = millis();
      int nextLane = (activeLane + 1) % LANE_COUNT;
      while (!laneVehicle[nextLane] && nextLane != activeLane) {
        nextLane = (nextLane + 1) % LANE_COUNT;
        if (nextLane == activeLane) {
          nextLane = 0;
          break;
        }
      }
      if (laneVehicle[nextLane]) {
        activeLane = nextLane;
      }
      updateTrafficState();
    }
  }

  updateOLED();
  delay(200);
}

void initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    while (true)
      ;
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
  display.setCursor(0, 0);
  display.print("Lane: ");
  display.println(activeLane + 1);

  display.print("IR: ");
  for (int i = 0; i < LANE_COUNT; i++) {
    display.print(laneVehicle[i] ? "1" : "0");
    if (i < 2) display.print(" ");
  }

  display.setCursor(0, 18);
  display.print("RFID: ");
  display.println(lastRFID);

  display.setCursor(0, 30);
  display.println(statusMessage);

  display.setCursor(0, 42);
  display.println(lastTagOwner);

  display.display();
}

void updateIRStatus() {
  for (int i = 0; i < LANE_COUNT; i++) {
    int value = digitalRead(IR_PINS[i]);
    if (IR_ACTIVE_LOW) {
      laneVehicle[i] = (value == LOW);
    } else {
      laneVehicle[i] = (value == HIGH);
    }
  }
}

void setAllRed() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(RED_PINS[i], LOW);
    digitalWrite(YELLOW_PINS[i], HIGH);
    digitalWrite(GREEN_PINS[i], HIGH);
  }
}

void setLaneGreen(int lane) {
  for (int i = 0; i < 3; i++) {
    digitalWrite(RED_PINS[i], LOW);
    digitalWrite(YELLOW_PINS[i], HIGH);
    digitalWrite(GREEN_PINS[i], HIGH);
  }

  digitalWrite(RED_PINS[lane], LOW);
  digitalWrite(YELLOW_PINS[lane], HIGH);
  digitalWrite(GREEN_PINS[lane], LOW);

  for (int i = 0; i < 3; i++) {
    if (i != lane) {
      digitalWrite(RED_PINS[i], LOW);
      digitalWrite(YELLOW_PINS[i], HIGH);
      digitalWrite(GREEN_PINS[i], HIGH);
    }
  }

  digitalWrite(RED_PINS[lane], HIGH);
  digitalWrite(YELLOW_PINS[lane], HIGH);
  digitalWrite(GREEN_PINS[lane], LOW);

  for (int i = 0; i < 3; i++) {
    if (i == lane) {
      digitalWrite(RED_PINS[i], HIGH);
      digitalWrite(YELLOW_PINS[i], HIGH);
      digitalWrite(GREEN_PINS[i], LOW);
    }
  }
}

void updateTrafficState() {
  for (int i = 0; i < 3; i++) {
    if (i == activeLane) {
      digitalWrite(RED_PINS[i], HIGH);
      digitalWrite(YELLOW_PINS[i], HIGH);
      digitalWrite(GREEN_PINS[i], LOW);
    } else {
      digitalWrite(RED_PINS[i], LOW);
      digitalWrite(YELLOW_PINS[i], HIGH);
      digitalWrite(GREEN_PINS[i], HIGH);
    }
  }
}

void readRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  if (millis() - previousRfidScanTime < rfidDebounce) {
    mfrc522.PICC_HaltA();
    return;
  }

  previousRfidScanTime = millis();
  lastRFID = printUID(mfrc522.uid);

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
    statusMessage = "Authorized Access";
    lastTagOwner = "Lane " + String(matchedLane + 1) + " priority";
    Serial.println("Authorized RFID for lane " + String(matchedLane + 1));
  } else {
    statusMessage = "Access denied";
    lastTagOwner = "Unauthorized tag";
    Serial.println("Unauthorized RFID tag");
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

#if BLYNK_ENABLE
BLYNK_WRITE(V0) {
  // Example virtual pin mapping for lane status
}
#endif
