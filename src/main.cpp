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
SoftwareSerial gestureSerial(18, 19);
DFRobot_Gesture_Touch dfgt(&gestureSerial);

unsigned long sendDataPrevMillis = 0;
int ledMode = 0; // 0: Auto/GPS, 1: Girofar (Gesture), 5: Alarm
unsigned long girofarLastToggle = 0;
bool girofarState = false;
unsigned long lastAlarmCheck = 0;

// --- Menu System Variables ---
enum MenuPage { MENU_DASHBOARD, MENU_MESSAGES, MENU_CONTROL };
MenuPage currentMenu = MENU_DASHBOARD;

const char *messages[] = {"I am Safe", "Coming Home", "Need Help"};
int messageIndex = 0;
int messageCount = 3;

const char *controls[] = {"Girofar ON/OFF", "Silent Mode", "Reboot"};
int controlIndex = 0;
int controlCount = 3;
bool silentMode = false;

// --- WiFi Gesture Manager Variables ---
enum WifiState {
  WIFI_SCAN,
  WIFI_SELECT,
  WIFI_PASS_ENTRY,
  WIFI_CONNECTING,
  WIFI_CONNECTED
};

WifiState wifiState = WIFI_SCAN;
int numNetworks = 0;
int currentNetworkIndex = 0;
String currentSSID = "";
String currentPass = "";
char currentChar = 'a'; // Start with lowercase 'a'
String availableChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01"
                        "23456789!@#$%^&*()_+-=[]{}|;:,.<>?";
int charIndex = 0;

// Helper to get gesture safely
int8_t getGesture() {
  int8_t rslt = dfgt.getAnEvent();
  if (rslt != 0) {
    // Basic debounce / log could go here
    return rslt;
  }
  return 0;
}

void updateLCD(String line1, String line2) {
  if (silentMode)
    return; // Don't update if silent
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void updateMenuInterface() {
  if (silentMode && currentMenu != MENU_CONTROL)
    return;

  switch (currentMenu) {
  case MENU_DASHBOARD: {
    String l1 = "Lat:" + String(gps.location.lat(), 3);
    String l2 = "Lon:" + String(gps.location.lng(), 3);
    if (!gps.location.isValid()) {
      l1 = "GPS: Searching..";
      l2 = "Sats: " + String(gps.satellites.value());
    }
    updateLCD(l1, l2);
  } break;
  case MENU_MESSAGES:
    updateLCD("Send Msg (Push):", String(messages[messageIndex]));
    break;
  case MENU_CONTROL:
    updateLCD("Control (Push):", String(controls[controlIndex]));
    break;
  }
}

void connectWiFiWithGestures() {
  wifiState = WIFI_SCAN;

  while (wifiState != WIFI_CONNECTED) {
    // Always poll gesture first
    int8_t gesture = getGesture();

    switch (wifiState) {

      // --- STATE: SCANNING ---
    case WIFI_SCAN:
      updateLCD("Scanning WiFi...", "Please Wait");
      numNetworks = WiFi.scanNetworks();
      if (numNetworks == 0) {
        updateLCD("No Networks", "Rescanning...");
        delay(2000);
      } else {
        // Auto-connect check
        bool autoConnect = false;
        for (int i = 0; i < numNetworks; i++) {
          String sName = String(WiFi.SSID(i).c_str());
          if (sName == "Udleniteli") {
            currentSSID = sName;
            currentPass = "3septembrie";
            wifiState = WIFI_CONNECTING;
            autoConnect = true;
            updateLCD("Auto-Link Found!", "udleniteli");
            delay(1500);
            break;
          }
        }

        if (!autoConnect) {
          currentNetworkIndex = 0;
          charIndex = 0;
          wifiState = WIFI_SELECT;
          // Initial display for select state
          String ssid = String(WiFi.SSID(0).c_str());
          if (ssid.length() > 16)
            ssid = ssid.substring(0, 16);
          updateLCD("Select Network:", ssid);
        }
      }
      break;

      // --- STATE: SELECT NETWORK ---
    case WIFI_SELECT:
      // Gestures: LEFT/RIGHT to scroll, FORWARD to select, BACK to rescan
      if (gesture == DFGT_EVT_RIGHT) {
        currentNetworkIndex++;
        if (currentNetworkIndex >= numNetworks)
          currentNetworkIndex = 0;
        String ssid = String(WiFi.SSID(currentNetworkIndex).c_str());
        if (ssid.length() > 16)
          ssid = ssid.substring(0, 16);
        updateLCD("Select Network:", ssid);
      } else if (gesture == DFGT_EVT_LEFT) {
        currentNetworkIndex--;
        if (currentNetworkIndex < 0)
          currentNetworkIndex = numNetworks - 1;
        String ssid = String(WiFi.SSID(currentNetworkIndex).c_str());
        if (ssid.length() > 16)
          ssid = ssid.substring(0, 16);
        updateLCD("Select Network:", ssid);
      } else if (gesture == DFGT_EVT_FORWARD) {
        currentSSID = String(WiFi.SSID(currentNetworkIndex).c_str());
        currentPass = "";
        currentChar = availableChars[charIndex];
        wifiState = WIFI_PASS_ENTRY;
        updateLCD("Pass: ", String(currentChar));
      } else if (gesture == DFGT_EVT_BACK) {
        wifiState = WIFI_SCAN;
      }
      break;

      // --- STATE: PASSWORD ENTRY ---
    case WIFI_PASS_ENTRY: {
      bool update = false;
      if (gesture == DFGT_EVT_RIGHT) {
        charIndex++;
        if (charIndex >= availableChars.length())
          charIndex = 0;
        currentChar = availableChars[charIndex];
        update = true;
      } else if (gesture == DFGT_EVT_LEFT) {
        charIndex--;
        if (charIndex < 0)
          charIndex = availableChars.length() - 1;
        currentChar = availableChars[charIndex];
        update = true;
      } else if (gesture == DFGT_EVT_FORWARD) {
        if (currentPass.length() < 30) {
          currentPass += currentChar;
          update = true;
        }
      } else if (gesture == DFGT_EVT_BACK) {
        if (currentPass.length() > 0) {
          currentPass.remove(currentPass.length() - 1);
          update = true;
        }
      } else if (gesture == DFGT_EVT_TOUCH1) {
        // Submit
        wifiState = WIFI_CONNECTING;
      } else if (gesture == DFGT_EVT_TOUCH5) {
        // Cancel
        wifiState = WIFI_SELECT;

        String ssid = String(WiFi.SSID(currentNetworkIndex).c_str());
        if (ssid.length() > 16)
          ssid = ssid.substring(0, 16);
        updateLCD("Select Network:", ssid);
        break; // Break helper
      }

      if (update) {
        String dispPass = currentPass;
        dispPass += currentChar;
        if (dispPass.length() > 16) {
          dispPass = dispPass.substring(dispPass.length() - 16);
        }
        updateLCD("Pass > " + String(currentChar), dispPass);
      }
    } break;

    // --- STATE: CONNECTING ---
    case WIFI_CONNECTING: {
      String ssid = currentSSID;
      if (ssid.length() > 16)
        ssid = ssid.substring(0, 16);
      updateLCD("Connecting to:", ssid);
    }
      WiFi.begin(currentSSID.c_str(), currentPass.c_str());

      unsigned long startAttempt = millis();
      bool connected = false;
      while (millis() - startAttempt < 10000) {
        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          break;
        }
        delay(500);
        Serial.print(".");
      }

      if (connected) {
        updateLCD("WiFi Connected!", "Success");
        delay(2000);
        wifiState = WIFI_CONNECTED;
      } else {
        updateLCD("Connect Failed", "Try Again");
        delay(2000);
        wifiState = WIFI_PASS_ENTRY; // Go back to pass entry
        String dispPass = currentPass;
        dispPass += currentChar;
        updateLCD("Pass > " + String(currentChar), dispPass);
      }
      break;
    }
    delay(100); // Small delay for loop
  }
}

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
  gestureSerial.begin(9600);
  // Important: Wait a bit for sensor to stabilize
  delay(1000);

  dfgt.setGestureDistance(20);

  // Init GPS Hardware Serial
  gpsHwSerial.begin(9600, SERIAL_8N1, GPS_C3_RX_PIN, GPS_C3_TX_PIN);

  // --- WiFi Gesture Setup ---
  connectWiFiWithGestures();

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  }
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  updateMenuInterface(); // Init Menu
}

void loop() {
  // 1. Read Gesture
  int8_t rslt = dfgt.getAnEvent();

  // High Priority: Alarm Dismiss
  if (rslt == DFGT_EVT_TOUCH5) {
    if (ledMode == 5) {
      Firebase.RTDB.setBool(&fbdo, "/alarm", false);
    }
    // Always turn off LED/Girofar
    ledMode = 0;
    pixels.clear();
    pixels.show();
    // Also acts as a "Cancel" or "Home" button
    currentMenu = MENU_DASHBOARD;
    updateMenuInterface();
  } else if (rslt != 0 && ledMode != 5) {
    // Menu Navigation (Only if not in Alarm Mode)
    bool update = false;

    // Page Navigation
    if (rslt == DFGT_EVT_RIGHT) {
      int page = (int)currentMenu + 1;
      if (page > 2)
        page = 0;
      currentMenu = (MenuPage)page;
      update = true;
    } else if (rslt == DFGT_EVT_LEFT) {
      int page = (int)currentMenu - 1;
      if (page < 0)
        page = 2;
      currentMenu = (MenuPage)page;
      update = true;
    }

    // Option Navigation (Pull Up/Down)
    else if (rslt == DFGT_EVT_PULLUP) {
      if (currentMenu == MENU_MESSAGES) {
        messageIndex++;
        if (messageIndex >= messageCount)
          messageIndex = 0;
        update = true;
      } else if (currentMenu == MENU_CONTROL) {
        controlIndex++;
        if (controlIndex >= controlCount)
          controlIndex = 0;
        update = true;
      }
    } else if (rslt == DFGT_EVT_PULLDOWN) {
      if (currentMenu == MENU_MESSAGES) {
        messageIndex--;
        if (messageIndex < 0)
          messageIndex = messageCount - 1;
        update = true;
      } else if (currentMenu == MENU_CONTROL) {
        controlIndex--;
        if (controlIndex < 0)
          controlIndex = controlCount - 1;
        update = true;
      }
    }

    // Action (Push/Forward)
    else if (rslt == DFGT_EVT_FORWARD) {
      if (currentMenu == MENU_MESSAGES) {
        updateLCD("Sending Msg...", String(messages[messageIndex]));
        Firebase.RTDB.setString(&fbdo, "/message", messages[messageIndex]);
        delay(1000);
        update = true;
      } else if (currentMenu == MENU_CONTROL) {
        if (controlIndex == 0) { // Girofar Toggle
          if (ledMode == 1)
            ledMode = 0;
          else
            ledMode = 1;
          update = true;
        } else if (controlIndex == 1) { // Silent Mode
          silentMode = !silentMode;
          if (silentMode) {
            lcd.noBacklight();
            lcd.clear();
          } else {
            lcd.backlight();
            update = true;
          }
        } else if (controlIndex == 2) { // Reboot
          updateLCD("Rebooting...", "Bye!");
          delay(1000);
          ESP.restart();
        }
      }
    }

    if (update)
      updateMenuInterface();
  }

  // 2. Handle Girofar (ledMode 1) & Alarm (ledMode 5)
  if (ledMode == 1) {
    // Standard Girofar
    if (millis() - girofarLastToggle > 100) {
      girofarLastToggle = millis();
      girofarState = !girofarState;
      if (girofarState)
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));
      else
        pixels.setPixelColor(0, pixels.Color(0, 0, 255));
      pixels.show();
    }
  } else if (ledMode == 5) {
    // Alarm Mode
    if (millis() - girofarLastToggle > 100) {
      girofarLastToggle = millis();
      girofarState = !girofarState;
      if (silentMode) {
        // In silent alarm, maybe just minimal indication or force wake?
        // Lets respect silent mode for now but maybe flash pixels only
      }

      if (girofarState) {
        pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red
        if (!silentMode) {
          lcd.setCursor(0, 0);
          lcd.print("   ALARM!!!   ");
        }
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue
        if (!silentMode) {
          lcd.setCursor(0, 0);
          lcd.print("              ");
        }
      }
      pixels.show();
    }
  } else if (ledMode == 0 && !silentMode) {
    // Auto Mode (GPS Status)
    // Keep existing logic but maybe less frequent updates to not block
  }

  // 3. Read GPS
  while (gpsHwSerial.available() > 0) {
    gps.encode(gpsHwSerial.read());
  }

  // 4. Update GPS LED/Status (Only if in Auto/GPS mode 0)
  // Also refresh Dashboard if checking GPS
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) {
    lastDebug = millis();

    // Update Dashboard if active
    if (currentMenu == MENU_DASHBOARD && ledMode != 5) {
      updateMenuInterface();
    }

    // GPS LED Logic (optional, keep if user liked it)
    if (ledMode == 0) {
      if (!gps.location.isValid()) {
        // pixels.setPixelColor(0, pixels.Color(0, 0, 255));
      } else {
        // pixels.setPixelColor(0, pixels.Color(0, 255, 0));
      }
      // pixels.show();
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
    }
  }

  // 6. Check for Remote Alarm
  if (Firebase.ready() && signupOK && (millis() - lastAlarmCheck > 2000)) {
    lastAlarmCheck = millis();
    if (Firebase.RTDB.getBool(&fbdo, "/alarm")) {
      if (fbdo.boolData()) {
        if (ledMode != 5) {
          ledMode = 5;
          if (silentMode) {
            silentMode = false; // Force wake on alarm
            lcd.backlight();
          }
          lcd.clear();
        }
      }
    }
  }
}