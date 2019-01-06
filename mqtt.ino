/**
 * MQTT
 */
 void sendSetupStatus() {
  if(mqttConnected) {
    // Create message
    char msg[128] = "";
    sprintf(msg,"{\"id\":\"%s\", \"type\":\"%s\"}", macaddress, IOT_DEVICE_TYPE);

    // Concat topic hello.
    char topic[16];
    sprintf(topic,"%s/%s", mqttSetupTopic, "hello");

    sendMqttMessage(topic, msg);
  }
}

void sendStatus(char* state) {
  if(strlen(mqttBaseTopic) > 0) {
    // Create message.
    char msg[128] = "";
    sprintf(msg,"{\"id\":\"%s\",\"type\":\"%s\"}", mqttBaseTopic, state);
    // Concat main topic status.
    char topic[64];
    sprintf(topic,"%s/%s", mqttBaseTopic, "status");
    sendMqttMessage(topic, msg);
  }
}

void sendMqttMessage(char* topic, char* msg) {
  if(mqttConnected) {
    if(debugMode) {
      Serial.print("Sending on topic: ");
      Serial.print(topic);
      Serial.println();
      Serial.print("With Message: ");
      Serial.print(msg);
      Serial.println();
    }
    client.publish(topic, msg);
  }
}

void setupMQTT() {
  client.setServer(mqttClientServer, 1883);
  client.setCallback(mqttEventHandler);
  mqttLoop();
}


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
  // parse json
  JsonObject& root = jsonBuffer.parseObject(msg);

  if(!root.success()) {
    if(debugMode) {
      Serial.println("parseObject() failed");
    }
    return;
  }

  if (!strcmp("setup/response", topic) && setupMode) {
    const char* macid = root["id"];
    const char* mainTopic = root["data"]["topic"];
    const char* lastCommand = root["data"]["lastCommand"];
    if (!strcmp(macid, macaddress)) {
      if(debugMode) {
        Serial.println("Finishing setup.");
      }
      displayMessage("Finishing setup.");
      setupMode = false;
      // Clean up topic.
      client.publish("setup/response", NULL, NULL, true);
      strcpy(mqttBaseTopic, mainTopic);
      if(lastCommand == "down") {
        lastDirection = 1;
      } else {
        lastDirection = -1;
      }
      writeRtcData();
      
      display.clearDisplay();
      display.display();
      digitalWrite(enableResetGPIO, HIGH);
      
      systemRestart();
    }
  }

  if (!strcmp(mqttCmdTopic, topic) && !setupMode) {
    const char* cmd = root["type"];
    // Clean up topic.
    client.publish(mqttCmdTopic, NULL, NULL, true);
    if (!strcmp(cmd, "down")) {
      currentDirection = 1;
      displayMessage("Down");
      sendStatus("down");
    } else if (!strcmp(cmd, "up")) {
      currentDirection = -1;
      displayMessage("Up");
      sendStatus("up");
    } else if (!strcmp(cmd, "stop")) {
      lastDirection = currentDirection;
      currentDirection = 0;
      displayMessage("Stopped");
      sendStatus("stopped_by_command");
    }
    if (debugMode) {
      Serial.print("Cmd: ");
      Serial.println(cmd);
      Serial.print("CurrentDirection: ");
      Serial.println(currentDirection);
      Serial.print("LastDirection: ");
      Serial.println(lastDirection);
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
    char topic[192];
    if(setupMode) {
      sprintf(topic,"%s/%s", mqttSetupTopic, "response");
    } else {
      strcpy(topic, mqttCmdTopic);
    }
    if (client.connect("homebru")) {
      mqttConnected = true;
      // subscribe
     client.subscribe(topic, 1);
     if(debugMode) {
        Serial.print("Connected to:");
        Serial.println(topic);
      }
      // send current status
      if(setupMode) {
        sendSetupStatus();
      } else {
        sendStatus("present");
      }
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
