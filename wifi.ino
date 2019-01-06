/**
 * WIFI
 */
void setupWifi() {
  // We start by connecting to a WiFi network

  // we use a fixed ip for quick access
  if (wifiFixedMode) {
    WiFi.config(fixedIP, router, subnet, router, router);
  } else {
    WiFi.mode(WIFI_STA);
  }

  // skip if crc error
  if (eepromCRCerror) {
    if (debugMode) {
      Serial.println("Start WPS.");
    }
    // skip the rest
    return;
  }
  
  if (strlen(eepromData.ssid) > 0 && strlen(eepromData.passphrase) > 0) {
    WiFi.begin(eepromData.ssid, eepromData.passphrase);
    wpsMode = false;
    if (debugMode) {
      Serial.println();
      Serial.print("Connecting to ");
      Serial.println(eepromData.ssid);
      Serial.print("MAC: ");
      Serial.println(WiFi.macAddress());
    }
  } else {
    if (debugMode) {
      Serial.println("Start WPS.");
    }
    // skip the rest
    return;
  }
  //delay(200);

  while (WiFi.status() != WL_CONNECTED && maxWifiConnectionRetries > wifiConnectionRetries) {
    delay(100);
    wifiConnectionRetries++;
    Serial.print("Waiting for connection to ");
    Serial.println(eepromData.ssid);
  }  
  
  if(WiFi.status() != WL_CONNECTED) {
    if(debugMode) {
      Serial.println("No WiFi connection");
    }
  } else {
    if(debugMode) {
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("Connection retries: ");
      Serial.println(wifiConnectionRetries);
    }
    wifiConnected = true;
  }
}

void wpsLoop() {
  if (wpsMode) {
    if (debugMode) {
      Serial.print("Start WPS config loop.");
    }
    bool wpsSuccess = WiFi.beginWPSConfig();
    if(wpsSuccess) {
      if (debugMode) {
        stats("WPS");
      }
      displayMessage("Wifi credentials\n received.");
      // Well this means not always success :-/ in case of a timeout we have an empty ssid
      String ssid = WiFi.SSID();
      
      // restart if ssid is empty.
      if (ssid == "") {
        if (debugMode) {
          Serial.println("SSID is empty - retry.");
        }
        return;
      }

      char ssidChar[ssid.length() +1];
      ssid.toCharArray(ssidChar, ssid.length() +1);
      strcpy(eepromData.ssid, ssidChar);
      String passphrase = WiFi.psk();
      char pskChar[passphrase.length() +1];
      passphrase.toCharArray(pskChar, passphrase.length() +1);
      strcpy(eepromData.passphrase, pskChar);
      
      
      // WPSConfig has already connected in STA mode successfully to the new station. 
      if (debugMode) {
        Serial.print("WPS finished. Connected successfull to SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("WPS PSK: ");
        Serial.println(WiFi.psk());
      }

      writeEepromData();
      
      display.clearDisplay();
      display.display();
      digitalWrite(enableResetGPIO, HIGH);

      systemRestart();
    }
  }
}
