#include "AConfig.h"
#if(HAS_MS5803_XXBA)

#include "Device.h"
#include "MS5803_XXBA.h"
#include "Settings.h"
#include "Timer.h"
#include <Wire.h>
#include "MS5803_XXBALib.h"

/*
Library for MS5803 14 and 30 bar sensors on OpenROV.

Author: brian@openrov.com
Based on works by Walt Holm and Luke Miller
*/



const int DevAddress = MS5803_XXBA_I2C_ADDRESS;  // 7-bit I2C address of the MS5803

// Here are the commands that can be sent to the 5803
// Page 6 of the data sheet
const byte Reset = 0x1E;
const byte D1_256 = 0x40;
const byte D1_512 = 0x42;
const byte D1_1024 = 0x44;
const byte D1_2048 = 0x46;
const byte D1_4096 = 0x48;
const byte D2_256 = 0x50;
const byte D2_512 = 0x52;
const byte D2_1024 = 0x54;
const byte D2_2048 = 0x56;
const byte D2_4096 = 0x58;
const byte AdcRead = 0x00;
const byte PromBaseAddress = 0xA0;
const bool FreshWater = 0;
const bool SaltWater = 1;
//io_timeout is the max wait time in ms for I2C requests to complete
const int io_timeout = 20;


unsigned int CalConstant[8];  // Matrix for holding calibration constants

long AdcTemperature, AdcPressure;  // Holds raw ADC data for temperature and pressure
float Temperature, Pressure, TempDifference, Offset, Sensitivity;
float T2, Off2, Sens2;  // Offsets for second-order temperature computation
float AtmosPressure = 1015;
float Depth;
float DepthOffset = 0;
float WaterDensity = 1.019716;
Timer DepthSensorSamples;
byte ByteHigh, ByteMiddle, ByteLow;  // Variables for I2C reads
bool WaterType = FreshWater;
static bool MS5803_inialized = false;

// Program initialization starts here

void sendCommand(byte command){
  Wire.beginTransmission(DevAddress);
  Wire.write(command);
  Wire.endTransmission();
}



void MS5803_XXBA::device_setup(){
  DepthSensorSamples.reset();
  Wire.beginTransmission(MS5803_XXBA_I2C_ADDRESS);
  if (Wire.endTransmission() != 0)
    return; //Cannot find I2c device, abort setup

  Settings::capability_bitarray |= (1 << DEPTH_CAPABLE);

  Serial.println("log:Depth Sensor setup;");
  Wire.begin();
  Serial.println("MS5803.status: initialized I2C;");
  delay(10);
  unsigned int cal;
  // Reset the device and check for device presence

  sendCommand(Reset);
  delay(10);
  Serial.println("MS5803.status: reset;");

  // Get the calibration constants and store in array

  for (byte i = 0; i < 8; i++)
  {
    sendCommand(PromBaseAddress + (2*i));
    Wire.requestFrom(DevAddress, 2);
    while(Wire.available()){
      ByteHigh = Wire.read();
      ByteLow = Wire.read();
    }
    CalConstant[i] = (((unsigned int)ByteHigh << 8) + ByteLow);
  }

  Serial.println("Depth: Calibration constants are:");

  for (byte i=0; i < 8; i++)
  {
    cal = CalConstant[i];
    Serial.print("Depth.C");Serial.print(i);Serial.print(":");Serial.print(cal);Serial.println(";");
    //log(CalConstant[i]);
  }


  MS5803_inialized = true;

}


void MS5803_XXBA::device_loop(Command command){
  if (!MS5803_inialized){
    if (DepthSensorSamples.elapsed(30000)){
      device_setup();
    }
    return;
  }

  if (command.cmp("dzer")){
    DepthOffset=Depth;
  }
  else if (command.cmp("dtwa")){
    if (Settings::water_type == FreshWater) {
      Settings::water_type = SaltWater;
      Serial.println(F("dtwa:1;"));
    } else {
      Settings::water_type =  FreshWater;
      Serial.println(F("dtwa:0;"));
    }
  }

  if (DepthSensorSamples.elapsed(1000)){
  // Read the Device for the ADC Temperature and Pressure values

  sendCommand(D1_512);
  delay(10);
  sendCommand(AdcRead);
  Wire.requestFrom(DevAddress, 3);

  unsigned int millis_start = millis();
  while (Wire.available() < 3) {
    if (io_timeout > 0 && ((unsigned int)millis() - millis_start) > io_timeout) {
      Serial.println("log:Failed to read Depth from I2C");
      return;
    }
  }
  ByteHigh = Wire.read();
  ByteMiddle = Wire.read();
  ByteLow = Wire.read();

  AdcPressure = ((long)ByteHigh << 16) + ((long)ByteMiddle << 8) + (long)ByteLow;

//  log("D1 is: ");
//  log(AdcPressure);

  sendCommand(D2_512);
  delay(10);
  sendCommand(AdcRead);
  Wire.requestFrom(DevAddress, 3);

  millis_start = millis();
  while (Wire.available() < 3) {
    if (io_timeout > 0 && ((unsigned int)millis() - millis_start) > io_timeout) {
      Serial.println("log:Failed to read Depth from I2C");
      return;
    }
  }
  ByteHigh = Wire.read();
  ByteMiddle = Wire.read();
  ByteLow = Wire.read();

  AdcTemperature = ((long)ByteHigh << 16) + ((long)ByteMiddle << 8) + (long)ByteLow;
 // log("D2 is: ");
//  log(AdcTemperature);

  envdata::TEMP = CorrectedTemperature(AdcTemperature, CalConstant);

  Pressure = TemperatureCorrectedPressure(AdcPressure, AdcTemperature, CalConstant);

  envdata::PRES = Pressure;

  // Convert to psig and display
  //
  //Pressure = Pressure - 1.015;  // Convert to gauge pressure (subtract atmospheric pressure)
  //Pressure = Pressure * 14.50377;  // Convert bars to psi
  //log("Pressure in psi is: ");
  //log(Pressure);

  if (Settings::water_type == FreshWater){
    //FreshWater
    Depth = (Pressure - AtmosPressure) * WaterDensity / 100;
  } else {
    // Salt Water
    // See Seabird App Note #69 for details
    // 9.72659 / 9.780318 = 0.9945

    Depth = (Pressure - AtmosPressure) * 0.9945 / 100;

  }
  navdata::DEEP = Depth-DepthOffset;

  }
}


#endif
