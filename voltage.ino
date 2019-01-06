void currentLoop() {
  if(currentDirection != 0) {
    long currentTime = millis();
    if (nextCurrentCheckInMillis < currentTime){
      //float average;
      
      //rawValue = analogRead(analogIn);
      //  average = average + (.0264 * analogRead(A0) -13.51);//for the 5A mode,  
      //average = average + (.049 * rawValue -45);// for 20A mode
      //average = average + (.742 * rawValue -37.8);// for 30A mode
      float U = 230;
  
      // To measure current we need to know the frequency of current
      // By default 50Hz is used, but you can specify desired frequency
      // as first argument to getCurrentAC() method, if necessary
      float I = sensor.getCurrentAC(50);
    
      // To calculate the power we need voltage multiplied by current
      if (debugMode) {
        float P = U * I;
        Serial.println(String("I = ") + I + " A");
        Serial.println(String("P = ") + P + " Watts");
        Serial.println(String("Lower or equal then 0.10A: ") + (I <= 0.10));
        Serial.println(String("Zero current count: ") + countZeroCurrent);
      }

      if (I <= 0.10) {
        countZeroCurrent++;
      } else {
        countZeroCurrent = 0;
      }

      if (countZeroCurrent > 4) {
        lastDirection = currentDirection;
        currentDirection = 0;
        displayMessage("Stopped");
        sendStatus("stopped_by_program");
      }
      
      nextCurrentCheckInMillis = currentTime + 100;
      
    }
  }
  if(currentDirection == 0) {
    long currentTime = millis();
    if (nextCurrentCalibrationCheckInMillis < currentTime) {
      float I = sensor.getCurrentAC(50);
      if (I > 0.10) {
        if (debugMode) {
          Serial.println(String("Recalibrate Sensor: ") + I + " A");
        }
        sensor.calibrate();
      }
      nextCurrentCalibrationCheckInMillis = currentTime + 1000;
    }
    
  }
}
