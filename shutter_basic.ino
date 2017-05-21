/**
 * shutter basic
 * sleep time - https://github.com/esp8266/Arduino/issues/1381
 * connects to wifi
 * connects to mqtt
 * checks status
 * - if new status opening or closing shutter
 * - update status
 * - send status on changed value every change as float and save value to rtc mem before sleepmode 
 *   - 0 = closed
 *   - 1 = open
 * - Button change between opening or closing
 * 
 * write into rtc mem
 * https://github.com/esp8266/Arduino/blob/master/libraries/esp8266/examples/RTCUserMemory/RTCUserMemory.ino
 * 
 */
 
/**
 * GPIO Mapping
 * static const uint8_t D0   = 16; // Reset - Deep Sleep
 * static const uint8_t D1   = 5;  // Relay Up
 * static const uint8_t D2   = 4;  // Relay Down
 * static const uint8_t D3   = 0;  // Switch
 * static const uint8_t D4   = 2;  // Status LED
 * static const uint8_t D5   = 14;
 * static const uint8_t D6   = 12;
 * static const uint8_t D7   = 13;
 * static const uint8_t D8   = 15;
 * static const uint8_t D9   = 3;
 * static const uint8_t D10  = 1;
 * safe gpios gpio4, gpio5, gpio12, gpio13, gpio14.
 */

 /**
  * Definitions
  */
#define MQTT_CONNECTION_TIMEOUT -1
#define MQTT_SOCKET_TIMEOUT 2
#define MQTT_KEEPALIVE 2
#define DEVICE_ID "shutter room"

/**
 * Additional Libraries
 */
#include <ESP8266WiFi.h> // wifi library
#include <PubSubClient.h> // mqtt client library
#include <Bounce2.h> // buttonswitch handling library
#include <EEPROM.h> // write/read eeprom library
#include <everytime.h> // timer library to set timeouts
#include <TaskScheduler.h> // addtional timing
extern "C" {
  #include "user_interface.h" // additional types library
}

/**
 * wlan settings
 */ 
const char* ssid = "Some WIFI SSID";
const char* wlanPassword = "SOME PW";

IPAddress router(192, 168, 178, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress fixedIP(192, 168, 178, 151);

/**
 * mqtt settings
 */
const char mqttClientServer[] = "192.168.178.150";
const char mqttClientId[] = "home/[room]/shutter/[id]"; // location/room/type/id
const char mqttSubscribeCommand[] = "home/[room]/shutter/[id]/set"; // location/room/type/id/command
const char mqttSubscribeOverwriteStatus[] = "home/[room]/shutter/[id]/state/set"; // location/room/type/id/set
const char mqttSubscribeGetStatus[] = "home/[room]/shutter/[id]/state/get"; // location/room/type/id/set
const char mqttPublishStatus[] = "home/[room]/shutter/[id]/state"; // location/room/type/id/state

/**
 * system settings
 */
const int sleepTimeInSeconds = 120;
const int prepareShutdownInSeconds = 3;
const byte relayUpGPIO = B101; // 5
const byte relayDownGPIO = B100; // 4
const byte switchGPIO = B0; // 0
const byte statusGPIO = B1110; // 14
const int nextMqttReconnecDelaytInSeconds = 3;
const int maxMqttConnectionAttempts = 1;
long nextShutdownInMillis = 0;
bool wifiConnected = false;
bool mqttConnected = false;
long nextMqttReconnecAttempt = 0;
int mqttConnectionAttempts = 0;
const bool debugMode = true;

/**
 * rtc mem struct
 */
struct {
  uint32_t crc32;
  float shutterStatus;
  int lastDirection;
} rtcData;

/**
 * shutter settings
 */
const float shutterDownTimeInSeconds = 20;
const float shutterUpTimeInSeconds = 25;

/**
 * System status
 */
bool errorStatus = false;
bool blinkStatus = true;

/**
 * shutter status
 */
float shutterGoal = 1.00;
float shutterStatus = 1.00;
int percentDownInMillis = 0;
int percentUpInMillis = 0;
long nextPercentInMillis = 0;
int lastDirection = 1; // has to be 1 cause on init the shutter is open

/**
 * instantiate objects
 */
Bounce debouncer = Bounce(); 
WiFiClient espClient;
PubSubClient client(espClient);

// CRC function used to ensure data validity
uint32_t calculateCRC32(const uint8_t *data, size_t length);

/**
 * Main Loop
 */
void loop() {
  // loop for local button
  buttonHandler();
  // shutterloop
  shutterLoop();
  // blink if error occurs
  blinkWhileError();
  // mqttloop for session handling or reconnecting
  mqttLoop();
}

/**
 * error status
 */
void blinkWhileError() {
  if(errorStatus) {
    if(blinkStatus) {
      digitalWrite(statusGPIO, LOW); 
      blinkStatus = false;
    } else {
      digitalWrite(statusGPIO, HIGH); 
      blinkStatus = true;
    }
    delay(500);
  }
}

/**
 * shutter loop
 */
void shutterLoop() {
  long currentTime = millis();
  // if equal skip
  if(!cmpfloat(shutterStatus, shutterGoal)) {
    digitalWrite(relayUpGPIO, HIGH);
    digitalWrite(relayDownGPIO, HIGH); 
    // check if next shutdown is passed even with 100ms additon
    if(nextShutdownInMillis+100 < currentTime) {
      nextShutdownInMillis = currentTime + prepareShutdownInSeconds * 1000;
      if(debugMode) {
        Serial.println("prepare shutdown");
      }
    } if(nextShutdownInMillis == currentTime) {
      sendStatus();
      // enter deep Sleep
      enterSleepMode();
    }
    return;
  }
  
  if(shutterStatus < shutterGoal) {
    digitalWrite(relayUpGPIO, LOW);
    digitalWrite(relayDownGPIO, HIGH); 
  }
  if(shutterStatus > shutterGoal) {
    digitalWrite(relayUpGPIO, HIGH);
    digitalWrite(relayDownGPIO, LOW); 
  }
  // if time epleased
  if( nextPercentInMillis < currentTime) {
    if(shutterStatus < shutterGoal) {
      nextPercentInMillis = currentTime + percentUpInMillis;
      shutterStatus += 0.01;
      lastDirection = 1;
    } else {
      nextPercentInMillis = currentTime + percentDownInMillis;
      shutterStatus -= 0.01;
      lastDirection = 0;
    }
    if(debugMode) {
      Serial.print("shutterStatus: ");
      Serial.println(shutterStatus);
    }
    // send status delaying the whole loop and the shutter timing is off
    // option: switch to global time instead of relative time
    //sendStatus();
  }
  
}

void sendStatus() {
  if(mqttConnected) {
    String msg_str;
    char msg[4];
    msg_str = String(shutterStatus);
    msg_str.toCharArray(msg, msg_str.length() + 1);
    client.publish(mqttPublishStatus, msg);
  }
}

/**
 * mqtt
 */
void mqttEventHandler(char* topic, byte* payload, unsigned int length) {
  // skip msg if empty to avoid loop
  if(length == 0) {
    return;
  }
  char msg[length];
  for (int i = 0; i < length; i++) {
    msg[i] = (char)payload[i];
  }

  if(debugMode) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.print(msg);
    Serial.println();
  }

  if(!strcmp(mqttSubscribeCommand ,topic)) {
    if(debugMode) {
      Serial.println("command via mqtt");
    }
    // message received we have to clear topic
    client.publish(mqttSubscribeCommand, NULL, NULL, true);
    setShutter(atof(msg));
  }
  if(!strcmp(mqttSubscribeOverwriteStatus ,topic)) {
    // clear message que
    client.publish(mqttSubscribeOverwriteStatus, NULL, NULL, true);
    // set status
    shutterStatus = atof(msg);
    shutterGoal = shutterStatus;
    // send status as confirmation
    sendStatus();
    if(debugMode) {
      Serial.println("overwrite status");
    }
  }
  if(!strcmp(mqttSubscribeGetStatus ,topic)) {
    sendStatus();
    
    if(debugMode) {
      Serial.println("get status");
    }
  }
}


boolean mqttConnect() {
  // Loop until we're reconnected
  if (!client.connected()) {
    if(debugMode) {
      Serial.print("Attempting MQTT connection...");
    }
    // Attempt to connect
    if (client.connect(mqttClientId)) {
      mqttConnected = true;
      if(debugMode) {
        Serial.println("connected");
      }
      // subscribe
      client.subscribe(mqttSubscribeCommand, 1);
      client.subscribe(mqttSubscribeOverwriteStatus, 1);
      // send current status
      sendStatus();
    } else {
      if(debugMode) {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in a view seconds");
      }
    }
  }
  return client.connected();
}

void mqttLoop() {
  // skip if max attempt reached
  if(mqttConnectionAttempts >= maxMqttConnectionAttempts) {
    return;
  }

  // skip if no wifi connection
  if(!wifiConnected) {
    return;
  }

  if(mqttConnected) {
    client.loop();
    return;
  }
  
  if (!client.connected()) {
    long currentTime = millis();
    if (nextMqttReconnecAttempt < currentTime) {
      // Attempt to reconnect
      if (mqttConnect()) {
        nextMqttReconnecAttempt = 0;
      } else {
        mqttConnectionAttempts++;
        nextMqttReconnecAttempt = currentTime + nextMqttReconnecDelaytInSeconds * 1000;
      }
    }
  }
}

/**
 * commandHandler
 */

void setShutter(float goal) {
  shutterGoal = goal;
}

void toggleCommand() {
  if(debugMode) {
    Serial.println("toggle Command");
    Serial.println(shutterGoal);
    Serial.println(lastDirection);
  }
  // set goal
  // if "running" => stop
  if(shutterStatus != shutterGoal) {
    if(shutterStatus < shutterGoal) {
      lastDirection = 1;
    } else {
      lastDirection = 0;
    }
    shutterGoal = shutterStatus;
  } else {
    if(lastDirection > 0) {
      setShutter(0);
    } else {
      setShutter(1);
    }
  }
  
}

/**
 * Button Handler
 */
void buttonHandler() {
  debouncer.update();
  if ( debouncer.fell() ) {
    toggleCommand();
  }
}

/**
 * SleepMode
 */
void enterSleepMode() {
  writeRtcData();
  if(debugMode) {
    Serial.println("Enter Deep Sleep");
  }
  ESP.deepSleep(sleepTimeInSeconds * 1e6); 
}


/** 
 *  Setup
 */
void setup() {
  // setup button
  setupGpio();
  // start debug mode
  if(debugMode) {
    Serial.begin(115200);
    Serial.print("\n");
    Serial.print("Start Debugmode\n");
  } 
      
  rst_info *resetInfo;
  resetInfo = ESP.getResetInfoPtr();
  if(debugMode) {
    Serial.println(resetInfo->reason);
  }
    
  if( resetInfo->reason == REASON_DEEP_SLEEP_AWAKE ) {
    // load state
    readRtcData();
    if(debugMode) {
      Serial.println("Deep Sleep Reset");
    }
  } else {
    if(debugMode) {
      Serial.println("Other Reset Reason");
    }
    // get status
  }
  
  // set 1 percent in millis
  setOnePercentInMillis();
  // setup wifi
  setupWifi();
  // setup mqtt
  setupMQTT();

  // setup finished
  digitalWrite(statusGPIO, HIGH);
}

void setOnePercentInMillis() {
  percentDownInMillis = shutterDownTimeInSeconds*10;
  percentUpInMillis = shutterUpTimeInSeconds*10;

  if(debugMode) {
    Serial.print("Percent down in Millis: ");
    Serial.println(percentDownInMillis); 
    Serial.print("Percent up in Millis: ");
    Serial.println(percentUpInMillis); 
  }
}

void setupGpio() {
  if(debugMode) {
    Serial.print("Setup GPIOs\n"); 
  }
  pinMode(relayUpGPIO, OUTPUT);
  pinMode(relayDownGPIO, OUTPUT);
  pinMode(statusGPIO, OUTPUT);

  digitalWrite(relayUpGPIO, HIGH);
  digitalWrite(relayDownGPIO, HIGH);
  
  pinMode(switchGPIO, INPUT_PULLUP);
  
  debouncer.attach(switchGPIO);
  debouncer.interval(50);
}

void setupWifi() {
  // We start by connecting to a WiFi network
  if(debugMode) {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  }

  // we use a fixed ip for quick access
  WiFi.config(fixedIP, router, subnet, router, router);
  WiFi.begin(ssid, wlanPassword);
  delay(200);

  if(WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
  }

  if(debugMode) {
    if(WiFi.status() != WL_CONNECTED) {
      Serial.println("No WiFi connection");
    } else {
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
    }
  }
  
}

void setupMQTT() {
  client.setServer(mqttClientServer, 1883);
  client.setCallback(mqttEventHandler);
  mqttLoop();
}

/**
 * helpers
 */
bool cmpfloat(float a, float b)
{
  return fabs(a - b) > 0.01;
}

uint32_t calculateCRC32(const uint8_t *data, size_t length)
{
  uint32_t crc = 0xffffffff;
  while (length--) {
    uint8_t c = *data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }
  return crc;
}

/**
 * read/write rtc data
 */

void readRtcData() {
  // Read struct from RTC memory
  if (ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
    if(debugMode) {
      Serial.println("Read RTC Mem");
    }
        
    uint32_t crcOfData = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);

    if (crcOfData != rtcData.crc32) {
      if(debugMode) {
        Serial.println("CRC32 in RTC memory doesn't match CRC32 of data. Data is probably invalid!");
      }
      errorStatus = true;
    }
    else {
      shutterStatus = rtcData.shutterStatus;
      shutterGoal = shutterStatus;
      lastDirection = rtcData.lastDirection;

      if(debugMode) {
        Serial.println("CRC32 check ok, data is probably valid.");
        Serial.print("shutterStatus: ");
        Serial.println(shutterStatus);
        Serial.print("lastDirection: ");
        Serial.println(lastDirection);
        Serial.print("shutterGoal: ");
        Serial.println(shutterGoal);
      }
    }
  }
}

void writeRtcData() {
  rtcData.shutterStatus = shutterStatus;
  rtcData.lastDirection = lastDirection;
  rtcData.crc32 = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);
  
  if (ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
    if(debugMode) {
      Serial.println("Write: RTC Mem");
      Serial.println();
    }
  }
}



