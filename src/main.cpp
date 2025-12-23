#include "addons/RTDBHelper.h"
#include "addons/TokenHelper.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <DFRobot_Gesture_Touch.h>
#include <Firebase_ESP_Client.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <Wire.h>

// Firebase Configuration
#define API_KEY "AIzaSyBqdrz5iSZvDLsfX8zHt6GmeVsNlBc6NxQ"
#define DATABASE_URL                                                           \
  "https://iot-project-65f02-default-rtdb.europe-west1.firebasedatabase.app/"

// WiFi Configuration
#define WIFI_SSID "Udleniteli"
#define WIFI_PASSWORD "3septembrie"

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RGB LED
#define PIN_RGB 8
#define NUM_PIXELS 1
Adafruit_NeoPixel pixels(NUM_PIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);

// GPS - Hardware Serial on Pins 6 (TX) and 7 (RX)
TinyGPSPlus gps;
HardwareSerial gpsHwSerial(1);
#define GPS_C3_RX_PIN 7
#define GPS_C3_TX_PIN 6

// Gesture Sensor - SoftwareSerial on Pins 18 (RX) and 19 (TX)
// WARNING: On ESP32-C3, 18/19 are USB pins.
// If USB drops, you may need to hold BOOT button to re-upload.
SoftwareSerial gestureSerial(18, 19);
DFRobot_Gesture_Touch dfgt(&gestureSerial);

unsigned long sendDataPrevMillis = 0;
int ledMode = 0; // 0: Auto/GPS, 1: Girofar (Gesture)
unsigned long girofarLastToggle = 0;
bool girofarState = false;

void setup() {
  Serial.begin(115200);

  // I2C for LCD
  Wire.begin(4, 5);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting System...");

  // RGB LED
  pixels.begin();
  pixels.clear();
  pixels.show();

  // Init Gesture Sensor
  // NOTE: If using 18/19, ensure wires are connected!
  gestureSerial.begin(9600);
  dfgt.setGestureDistance(20);

  // Init GPS Hardware Serial
  gpsHwSerial.begin(9600, SERIAL_8N1, GPS_C3_RX_PIN, GPS_C3_TX_PIN);

  // WiFi Connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 10000) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print("WiFi Connected");
  }

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  }
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  // 1. Read Gesture
  int8_t rslt = dfgt.getAnEvent();
  if (rslt != 0) {
    String gesture = "";
    switch (rslt) {
    case DFGT_EVT_BACK:
      gesture = "BACK";
      break;
    case DFGT_EVT_FORWARD:
      gesture = "FORWARD";
      break;
    case DFGT_EVT_RIGHT:
      gesture = "RIGHT";
      break;
    case DFGT_EVT_LEFT:
      gesture = "LEFT";
      break;
    case DFGT_EVT_PULLUP:
      gesture = "PULL UP";
      break;
    case DFGT_EVT_PULLDOWN:
      gesture = "PULL DOWN";
      break;
    case DFGT_EVT_PULLREMOVE:
      gesture = "PULL REMOVE";
      break;
    case DFGT_EVT_TOUCH1:
      gesture = "TOUCH 1 (Girofar)";
      ledMode = 1;
      break;
    case DFGT_EVT_TOUCH2:
      gesture = "TOUCH 2";
      ledMode = 2; // Manual Red
      pixels.setPixelColor(0, pixels.Color(255, 0, 0));
      pixels.show();
      break;
    case DFGT_EVT_TOUCH3:
      gesture = "TOUCH 3";
      ledMode = 3; // Manual Green
      pixels.setPixelColor(0, pixels.Color(0, 255, 0));
      pixels.show();
      break;
    case DFGT_EVT_TOUCH4:
      gesture = "TOUCH 4";
      ledMode = 4; // Manual Purple
      pixels.setPixelColor(0, pixels.Color(128, 0, 128));
      pixels.show();
      break;
    case DFGT_EVT_TOUCH5:
      gesture = "TOUCH 5";
      ledMode = 0; // Reset to Auto
      pixels.clear();
      pixels.show();
      break;
    default:
      gesture = "UNKNOWN";
      break;
    }

    if (gesture != "UNKNOWN") {
      lcd.setCursor(0, 1);
      lcd.print("Gest:           ");
      lcd.setCursor(6, 1);
      lcd.print(gesture);
    }
  }

  // 2. Handle Girofar
  if (ledMode == 1) {
    if (millis() - girofarLastToggle > 100) {
      girofarLastToggle = millis();
      girofarState = !girofarState;
      if (girofarState)
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));
      else
        pixels.setPixelColor(0, pixels.Color(0, 0, 255));
      pixels.show();
    }
  }

  // 3. Read GPS
  while (gpsHwSerial.available() > 0) {
    gps.encode(gpsHwSerial.read());
  }

  // 4. Update GPS LED/Status (Only if in Auto/GPS mode 0)
  // Check every 2 seconds to not spam neopixel
  static unsigned long lastDebug = 0;
  if (ledMode == 0 && millis() - lastDebug > 2000) {
    lastDebug = millis();
    if (gps.charsProcessed() < 10) {
      // Wiring Fail
    } else if (!gps.location.isValid()) {
      // Blue - Data OK, No Fix
      pixels.setPixelColor(0, pixels.Color(0, 0, 255));
      pixels.show();
    } else {
      // Green for Fix
      pixels.setPixelColor(0, pixels.Color(0, 255, 0));
      pixels.show();
    }
  }

  // 5. Send to Firebase
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 5000)) {
    sendDataPrevMillis = millis();
    if (gps.location.isValid()) {
      FirebaseJson json;
      json.set("lat", gps.location.lat());
      json.set("lon", gps.location.lng());
      Firebase.RTDB.setJSON(&fbdo, "/location", &json);

      // Update LCD with Coords
      if (ledMode == 0) {
        lcd.setCursor(0, 0);
        lcd.print(gps.location.lat(), 4);
        lcd.print(",");
        lcd.print(gps.location.lng(), 4);
      }
    }
  }
}