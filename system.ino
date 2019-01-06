/**
 * System
 */
void setupSystem() {
  // start debug mode
  if(debugMode) {
    Serial.begin(115200);
    Serial.print("\n");
    Serial.print("Start Debugmode\n");
  }
  // set mac address to memory
  String mac = WiFi.macAddress();
  mac.toCharArray(macaddress, mac.length() + 1);

  // get eeprom data
  readEepromData();
      
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
  }
  // check if setupMode
  if(strlen(mqttBaseTopic) > 0) {
    setupMode = false;
  } else {
    setupMode = true;
  }
  if(debugMode) {
    Serial.print("Debugmode: ");
    Serial.println(setupMode);
    Serial.print("MainTopicLength: ");
    Serial.println(strlen(mqttBaseTopic));
  }

  // calibrate sensor
  sensor.calibrate();
}
/**
 * GPIO
 */
void setupGpio() {
  if(debugMode) {
    Serial.println("Setup GPIOs\n"); 
  }
  // OUTPUT
  // Disable Reset Btn function.
  pinMode(enableResetGPIO, OUTPUT);
  digitalWrite(enableResetGPIO, LOW);
  // Relay Up
  pinMode(relayUpGPIO, OUTPUT);
  digitalWrite(relayUpGPIO, HIGH);
  // Relay Down
  pinMode(relayDownGPIO, OUTPUT);
  digitalWrite(relayDownGPIO, HIGH);
  
  // INPUT
  pinMode(switchGPIO, INPUT_PULLUP);
  
  debouncer.attach(switchGPIO);
  debouncer.interval(50);
}

/**
 * Button Handler
 */
void buttonHandler() {
  debouncer.update();
  if ( debouncer.fell() ) {
    if (debugMode) {
      Serial.println("Button pressed.");
    }
    toggleShutter();
  }
  // reset handler
  bool switchGPIOValue = digitalRead(switchGPIO);
  if (switchGPIOValue == false) {
    unsigned long currentTime = millis();
    if (buttonPressTimeStamp == 0) {
      buttonPressTimeStamp = currentTime;
      if (debugMode) {
        Serial.println("Starting reset.");
      }
    } else {
      int diff = (currentTime - buttonPressTimeStamp) / 1000;

      if (diff == 8) {
        displayMessage("4 seconds\n till reset.");
      }
      if (diff == 9) {
        displayMessage("3 seconds\n till reset.");
      }
      if (diff == 10) {
        displayMessage("2 seconds\n till reset.");
      }
      if (diff == 11) {
        displayMessage("1 seconds\n till reset.");
      }
      if (diff == 12) {
        displayMessage("Device resets now.");
        // clear eeprom
        strcpy(eepromData.ssid, "");
        strcpy(eepromData.passphrase, "");
        // clear rtc
        strcpy(rtcData.topic, "");
        // write
        writeRtcData();
        writeEepromData();
        // restart
        display.clearDisplay();
        display.display();
        digitalWrite(enableResetGPIO, HIGH);
        
        ESP.deepSleep(1 * 1e6); 
      }
    }
  } else if (buttonPressTimeStamp != 0 && switchGPIOValue == true) {
    // reset timer
    buttonPressTimeStamp = 0;
    unsigned long currentTime = millis();
    int diff = (currentTime - buttonPressTimeStamp) / 1000;
    if (diff > 8) {
      display.clearDisplay();
      display.display();
    }
  }
}

void toggleShutter() {
  if (debugMode) {
    Serial.print("Current direction: ");
    Serial.println(currentDirection);
  }
  if(currentDirection == 0) {
    currentDirection = -1*lastDirection;
    lastDirection = currentDirection;
    if(currentDirection == 1) {
      displayMessage("Down");
      sendStatus("down");
    } else {
      displayMessage("Up");
      sendStatus("up");
    }
  } else {
    currentDirection = 0;
    displayMessage("Stopped");
    sendStatus("stopped_by_command");
  }
  if (debugMode) {
    Serial.print("New direction: ");
    Serial.println(currentDirection);
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
  displayMessage("Good night!");
  delay(500);
  display.clearDisplay();
  display.display();
  digitalWrite(enableResetGPIO, HIGH);
  
  ESP.deepSleep(sleepTimeInSeconds * 1e6); 
}

/**
 * RTC
 */
void writeRtcData() {
  strcpy(rtcData.topic, mqttBaseTopic);
  rtcData.lastDirection = lastDirection;
  rtcData.crc32 = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);
  
  if (ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
    if(debugMode) {
      Serial.println("Write RTC Memory.");
      Serial.println();
    }
  }
}

void readRtcData() {
  // Read struct from RTC memory
  if (ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
    rtcCRCerror = true;
    if(debugMode) {
      Serial.println("Read RTC Memory");
    }
        
    int32_t crcOfData = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);

    if (crcOfData != rtcData.crc32) {
      if(debugMode) {
        Serial.println("CRC32 in RTC memory doesn't match CRC32 of data. Data is probably invalid!");
      }
    }
    else {
      strcpy(mqttBaseTopic, rtcData.topic);
      lastDirection = rtcData.lastDirection;

      if (strlen(mqttBaseTopic) > 0) {
        char topic[128] = "";
        sprintf(topic,"%s/%s", rtcData.topic, "cmd");
        strcpy(mqttCmdTopic, topic);
      }

      if(debugMode) {
        Serial.println("CRC32 check ok, data is valid.");
        Serial.print("topic: ");
        Serial.println(mqttBaseTopic);
        Serial.print("cmdtopic: ");
        Serial.println(mqttCmdTopic);
        Serial.print("lastDirection: ");
        Serial.println(lastDirection);
      }
    }
  }
}

void writeEepromData() {
  eepromData.crc32 = calculateCRC32(((uint8_t*) &eepromData) + 4, sizeof(eepromData) - 4);
  EEPROM.begin(sizeof(eepromData)+1);
  EEPROM.put(0, eepromData);
  EEPROM.commit();
  EEPROM.end();
  if(debugMode) {
    Serial.println("Write EEPROM.");
    Serial.print("Write crc32: ");
    Serial.println(eepromData.crc32);
    Serial.print("Write ssid: ");
    Serial.println(eepromData.ssid);
    Serial.print("Write passphrase: ");
    Serial.println(eepromData.passphrase);
    Serial.print("With datasize of: ");
    Serial.println(sizeof(eepromData));
    Serial.println();
  }
  
}

void readEepromData() {
  // Read struct from RTC memory
  EEPROM.begin(sizeof(eepromData)+1);
  EEPROM.get(0, eepromData);
  EEPROM.commit();
  EEPROM.end();
  if(debugMode) {
    Serial.println("Read EEPROM.");
  }
  
  uint32_t crcOfData = calculateCRC32(((uint8_t*) &eepromData) + 4, sizeof(eepromData) - 4);
  
  if (crcOfData != eepromData.crc32) {
    eepromCRCerror = true;
    if(debugMode) {
      Serial.println("CRC32 in EEPROM doesn't match CRC32 of data. Data is probably invalid!");
    }
    // clear data and restart.
    strcpy(eepromData.ssid, "");
    strcpy(eepromData.passphrase, "");
    writeEepromData();
    systemRestart();
  }
  else {
    if(debugMode) {
      Serial.println("EEPROM CRC32 check ok, data is valid.");
      Serial.println(String("EEPROM ssid: #") + eepromData.ssid + "#");
      Serial.println(String("EEEPROM psk: #") + eepromData.passphrase + "#");
    }
  }
}

uint32_t calculateCRC32(const uint8_t *data, size_t length) {
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

void stats(const char* what) {
  // we could use getFreeHeap() getMaxFreeBlockSize() and getHeapFragmentation()
  // or all at once:
  uint32_t free;
  uint16_t max;
  uint8_t frag;
  ESP.getHeapStats(&free, &max, &frag);

  Serial.printf("free: %5d - max: %5d - frag: %3d%% <- ", free, max, frag);
  // %s requires a malloc that could fail, using println instead:
  Serial.println(what);
}

void systemRestart() {
  if (debugMode) {
    Serial.println("restart via 1sec deepsleep.");
  }
  ESP.deepSleep(1 * 1e6);
}
