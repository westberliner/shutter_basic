/**
 * Description
 */

 /**
 * GPIO Mapping
 * static const uint8_t D0   = 16; // Reset - Deep Sleep
 * static const uint8_t D1   = 5;  // OLED SCL
 * static const uint8_t D2   = 4;  // OLED SDA
 * static const uint8_t D3   = 0;
 * static const uint8_t D4   = 2;  // Button
 * static const uint8_t D5   = 14; // Relay Up
 * static const uint8_t D6   = 12; // Enable Reset
 * static const uint8_t D7   = 13; // Relay Down
 * static const uint8_t D8   = 15; // Voltage Meter
 * static const uint8_t D9   = 3;
 * static const uint8_t D10  = 1;
 * safe gpios gpio4, gpio5, gpio12, gpio13, gpio14.
 */

/**
 * Additional Libraries
 */
#include <ESP8266WiFi.h> // Wifi library.
#include <PubSubClient.h> // Mqtt client library. | https://github.com/knolleary/pubsubclient
#include <ArduinoJson.h> // Encode/Decode json.
#include <Bounce2.h> // buttonswitch handling library.
#include <Adafruit_SSD1306.h> // Controll display library.
#include <ArduinoJson.h> // Json library.
#include "ACS712.h" // Get accurate measurements from sensor. | https://github.com/rkoptev/ACS712-arduino
#include <EEPROM.h>


/**
 * Definitions
 */
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET   -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// Device Type
#define IOT_DEVICE_TYPE "shutter"

/**
 * System Memory
 */
struct {
  uint32_t crc32;
  char topic[128];
  int lastDirection;
} rtcData;

struct {
  uint32_t crc32;
  char ssid[36];
  char passphrase[128];
} eepromData;

/**
 * System Settings
 */
// Digital IO
const byte switchGPIO = B10; // 2
const byte enableResetGPIO = B1100; // 12

const byte relayUpGPIO = B1110; // 14
const byte relayDownGPIO = B1101; // 13

// System Settings
const int sleepTimeInSeconds = 120;
const int prepareShutdownInSeconds = 10;
const bool wifiFixedMode = false;
const int maxWifiConnectionRetries = 50;
const int nextMqttReconnecDelaytInSeconds = 3;
const int maxMqttConnectionAttempts = 5;

// Development
const bool debugMode = false;

/**
 * WIFI Settings
 */
char macaddress[17];

IPAddress router(192, 168, 178, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress fixedIP(192, 168, 178, 151); // not used, dhcp is fast enough

/**
 * mqtt settings
 */
const char mqttClientServer[] = "homebru";
const char mqttSetupTopic[] = "setup"; 
char mqttBaseTopic[128] = ""; // [location]/[room]/[id]/[device-type]/[id]
char mqttCmdTopic[128] = "";

/**
 * System States
 */
bool rtcCRCerror = false;
bool eepromCRCerror = false;
unsigned long buttonPressTimeStamp;

long nextShutdownInMillis = 0;
bool wifiConnected = false;
int wifiConnectionRetries = 0;

bool mqttConnected = false;
long nextMqttReconnecAttempt = 0;
int mqttConnectionAttempts = 0;

bool setupMode = true;
bool wpsMode = true;
int lastDirection = 1;
int currentDirection = 0;

long nextCurrentCheckInMillis = 0;
long nextCurrentCalibrationCheckInMillis = 0;
int countZeroCurrent = 0;


/**
 * instantiate objects
 */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Bounce debouncer = Bounce();
WiFiClient espClient;
PubSubClient client(espClient);
StaticJsonBuffer<200> jsonBuffer;
ACS712 sensor(ACS712_30A, A0);

/**
 * define functions
 */
void displayMessage(String str, float textSize = 1.8);

void setup() {
  // read RTC data
  setupSystem();
  // Prepare pins for io.
  setupGpio();
  // Dispay first message.
  setupDisplay();
  // Send message for awaiting input.
  displayMessage("Connecting\n...");
  // setup wifi
  setupWifi();
  // setup mqtt
  setupMQTT();
  // Display connections.
  if (mqttConnected && wifiConnected) {
    if (setupMode) {
      displayMessage("Connected!\nWait for setup.");
    } else {
      displayMessage("Connected!\nWait for user.");
    }
  } else {
    if (setupMode) {
      if (wpsMode) {
        displayMessage("No wifi config.\nWait for wps.");
      } else {
        displayMessage("No wifi connect.\nRestart device.");
      }
    } else {
      displayMessage("No wifi connection.\nWait for user");
    }
  }

}

void loop() {
  // Loop for local button.
  buttonHandler();
  // wifi loop for wps
  wpsLoop();
  // Mqttloop for session handling or reconnecting.
  mqttLoop();
  // main 
  mainLoop();
  // shutter
  shutterLoop();
  // get Current
  currentLoop();
}

/**
 * Main Loop
 */
void mainLoop() {
  unsigned long currentTime = millis();
  
  if(nextShutdownInMillis+100 < currentTime) {
    nextShutdownInMillis = currentTime + prepareShutdownInSeconds * 1000;
    if(debugMode) {
      Serial.println("prepare shutdown");
    }
  }
  if(nextShutdownInMillis == currentTime && !setupMode && currentDirection == 0 && buttonPressTimeStamp == 0) {
    // enter deep Sleep
    enterSleepMode();
  } else if(nextShutdownInMillis == currentTime && !setupMode && currentDirection == 0) {
    nextShutdownInMillis = currentTime + prepareShutdownInSeconds * 1000; 
  } else if (nextShutdownInMillis == currentTime && setupMode) {
   if(debugMode) {
    //Serial.println("Skip shutown as long as we are in setupmode.");
   }
   
   nextShutdownInMillis = currentTime + prepareShutdownInSeconds * 1000; 
  }

}

void shutterLoop() {
  if(currentDirection == 1) {
    digitalWrite(relayUpGPIO, LOW);
    digitalWrite(relayDownGPIO, HIGH);
  }
  if(currentDirection == -1) {
    digitalWrite(relayUpGPIO, HIGH);
    digitalWrite(relayDownGPIO, LOW);
  }
  if(currentDirection == 0) {
    digitalWrite(relayUpGPIO, HIGH);
    digitalWrite(relayDownGPIO, HIGH);
  }
}
