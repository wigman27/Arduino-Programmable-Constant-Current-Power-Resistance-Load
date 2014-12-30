/*
Version Control

1.00
Initial upload

1.10
* Fixed an error in the set current function of the DAC
* Added improved current calculation to allow for correction in OP-AMP offset, uses the measured value to adjust the set current.
* General code tidy up

1.11
* Added prefixes to constants and gobal variables
* fixed adjusted current percentage calculation
* renamed DAC variables cleaned up equation

1.12
* foxed adjusted current percentage calucaltion ... again

To - Dos
* Improve push button functions to allow the encode to set the individual values ie. 10s, 1s, 0.s, 0.1s, 0.01s and 0.001s
* Display in engineering units, ie. mA and mW.
* Add PWM control to LCD backlight.

*/

// include the library code:
#include <LiquidCrystal.h>
#include <SPI.h>
#include <ClickEncoder.h>
#include <TimerOne.h>

// Set Constants
const int k_adcChipSelectPin = 8;      // set pin 8 as the chip select for the ADC:
const int k_adcInputVoltage = 0;       // set the ADC channel that reads the input voltage.
const int k_adcMeasuredCurrent = 1;    // set the ADC channel that reads the input current by measuring the voltage on the input side of the sense resistors.
const int k_adcTempSense1 = 2;         // set the ADC channel that reads the temprature sensor 1 under the heatsink.
const int k_adcTempSense2 = 3;         // set the ADC channel that reads the temprature sensor 2 under the heatsink.

const int k_dacChipSelectPin = 9;      // set pin 9 as the chip select for the DAC:
const int k_dacCurrentSet = 0;         // set The DAC channel that sets the constant current.
const int k_dacFanSet = 1;             // set The DAC channel that sets the fan speed.

const int k_encoderA = 3;              // set pin 3 as the channel A for encoder 1, int.0:
const int k_encoderB = 2;              // set pin 2 as the channel B for encoder 1, int.1:
const int k_encoderPBPin = 0;          // set pin 0 as the push button for encoder 1

const int k_ledBacklight = 11;         //LCD Backlight

//Menu constants
const int k_displayValues = 0;         // Constant used for the LCD to display the values
const int k_displayMenu = 1;           // Constant used for the LCD to display the menu


// Modes of operations
const int k_currentMode = 0;           // Represents the constant current mode
const int k_resistanceMode = 1;        // Represents the constant resistance mode
const int k_powerMode = 2;             // Represents the constant power mode

// Set integres for maximum values.
const int k_maximumCurrent = 8;        // Maximum Value of load current
const int k_maximumPower = 50;         // Maximum power dissipated

const float k_softwareVersion = 1.12;   // used for the current software version

// Set Integers
int g_encoderOldPos = -1;              // variable to store the old encoder position.
int g_encoderPos = 0;                  // variable to store the new encoder position.
int g_lcdDisplay = 0;                  // variable to store the LCD display mode, ie. Menu or Values.
int g_selectedMenu = 0;                // Which menu is selected when the button is pushed.
int g_mode = 0;                        // variable to store the mode the unit is in, ie. Constant Current, Resistance or Power.

// Set Floats
float g_inputVoltage = 0;              // Float that stores the input voltage.
float g_measuredCurrent = 0;           // Float that stores the measured current through the sustem.
float g_roundedMeasuredCurrent = 0;    // Used to round the float values to minamise DAC communications.
float g_setCurrent = 0;                // Float that stores the current sent to the DAC.
float g_setResistance = 0;             // Float that stores the calculated system resistance.
float g_setPower = 0;                  // Float that stores the calculated system power.
float g_adjustedCurrent =0;

// Used to refresh LCD display.
unsigned long g_timeSinceLastDisplay = 0;

// Set Encoder
ClickEncoder *encoder;
boolean encoderMoved = false;        // boolean to store if the encoder has moved.

void timerIsr() {
  encoder->service();                // Used by the encoder library.
}

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(10, 12, 4, 13, 6, 5);  // ATMega32U4 Pins RS-30, E-26, D4-25, D5-32, D6-27, D7-31.

// Start setup function:
void setup() {
  // attach interrupts:
  attachInterrupt(2, showMenu, FALLING);    //Sets encoder button interrupt on transition from high to low.
  // set outputs:
  pinMode (k_adcChipSelectPin, OUTPUT);
  pinMode (k_dacChipSelectPin, OUTPUT);
  pinMode (k_ledBacklight, OUTPUT);
  // set inputs:
  pinMode(k_encoderA, INPUT); 
  pinMode(k_encoderB, INPUT);
  // set the ChipSelectPins high initially:
  digitalWrite(k_adcChipSelectPin, HIGH);
  digitalWrite(k_dacChipSelectPin, HIGH);
  // set the LCD Backlight high
  digitalWrite(k_ledBacklight, HIGH);
  // initialise SPI:
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);         // Not strictly needed but just to be sure.
  SPI.setDataMode(SPI_MODE0);        // Not strictly needed but just to be sure.
  // Set SPI clock divider to 16, therfore a 1 MhZ signal due to the maximum frequency of the ADC.
  SPI.setClockDivider(SPI_CLOCK_DIV16);
  //initialise encoder and associated timers:
  encoder = new ClickEncoder(k_encoderA, k_encoderB, k_encoderPBPin);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
  // set up the LCD's number of columns and rows: 
  lcd.begin(20, 4);
  // Print a message to the LCD.
  lcd.setCursor(5, 1);
  lcd.print("Dummy Load");
  lcd.setCursor(4, 2);
  lcd.print("Version ");
  lcd.print(k_softwareVersion);
  delay(1000); 
  Serial.begin(9600);  
} // End setup function.

// Start loop function:
void loop() {  
  g_encoderPos += encoder->getValue();
  if (g_encoderPos != g_encoderOldPos) {
    encoderMoved = true;
    g_encoderOldPos = g_encoderPos;
  } else {
     encoderMoved = false;
  }  
  // Reads input voltags from the load source. ****MAXIMUM 24V INPUT**** 
  g_inputVoltage = readInputVoltage();
  // Calculates and sets required load current. Accepts the mode variable which defines the mode of the unit, ie. Constant current, resistance or power.  
  setLoadCurrent(g_mode);
  // Calculates heatsink temprature and sets fan speed accordingly.  
  setFanSpeed();
  // Updates the LCD display. Accepts the lcdDisplay variable which defines if the values or menu is to be displayed.
  updateLCD(g_lcdDisplay); 
}// End of loop function.

// Start of custom functions:

// Function to read the input voltage and return a float number represention volts.
float readInputVoltage() {
  g_inputVoltage = (readAdc(k_adcInputVoltage)) * 12.03;
  if (g_inputVoltage < 0.018) {
    g_inputVoltage = 0;
  }  
  return g_inputVoltage;
}

// Function to measure the actual load current.
float readMeasuredCurrent() {
   measuredCurrent = (readAdc(k_adcMeasuredCurrent)) / 0.1000;
}

//Function to calculate and set the required load current. Accepts the mode variable to determine if the constant current, resistance or power mode is to be used.
void setLoadCurrent (int setMode) {
  if (g_lcdDisplay != k_displayMenu) {  //To ensure the encoder position is not taken from the menu.
    switch (setMode) {
      case 0:
      // Current Mode            
      g_setCurrent = g_encoderPos / 500.000; // as the DAC is capable of increasing the current in 0.002A
      if (g_setCurrent < 0) g_setCurrent = 0;
      if (g_setCurrent > k_maximumCurrent) g_setCurrent = k_maximumCurrent;      
      g_setPower = g_inputVoltage * g_setCurrent;
      // Safety feature to ensure the maximum power value is not exceeded.
      if (g_setPower > k_maximumPower) {
        g_setCurrent = k_maximumPower / g_inputVoltage;
        g_encoderPos = int(g_setCurrent * 500.000);
      }
      // Calculate set resistance
      g_setResistance = g_inputVoltage / g_setCurrent;
      break;
      case 1:
      // Resistance Mode            
      g_setResistance = g_encoderPos / 100.000;
      if (g_setResistance < 0) g_setResistance = 0;      
      g_setCurrent = g_inputVoltage / g_setResistance;
      // Safety feature to ensure the maximum current value is not exceeded.
      if (g_setCurrent > k_maximumCurrent) {
        g_setCurrent = k_maximumCurrent;
        g_setResistance = g_inputVoltage / k_maximumCurrent;
        g_encoderPos = int(g_setResistance * 100.00);
      }
      g_setPower = g_inputVoltage * g_setCurrent;
      // Safety feature to ensure the maximum power value is not exceeded.      
      if (g_setPower > k_maximumPower) {
        g_setCurrent = k_maximumPower / g_inputVoltage;
        g_setResistance = k_maximumPower / (g_setCurrent * g_setCurrent);
        g_encoderPos = int(g_setResistance * 100.00);
      }
      break;
      case 2:
      // Power Mode            
      g_setPower = g_encoderPos / 100.000;
      if (g_setPower < 0) g_setPower = 0;
      if (g_setPower > k_maximumPower) g_setPower = k_maximumPower;      
      g_setCurrent = g_setPower / g_inputVoltage;
      // Safety feature to ensure the maximum current value is not exceeded.
      if (g_setCurrent > k_maximumCurrent) {
        g_setCurrent = k_maximumCurrent;
        g_setPower = g_inputVoltage * k_maximumCurrent;
        g_encoderPos = int(g_setPower * 100.00);
      }
      // Calculate set resistance
      g_setResistance = g_inputVoltage / g_setCurrent;
      break;    
    }
  }
  // Convert the set current into an voltage to be sent to the DAC
  g_measuredCurrent = readMeasuredCurrent();
  // To ensure we are not dividing by 0.
  if(measuredCurrent != 0) {
  g_adjustedCurrent = (abs(g_setCurrent - g_measuredCurrent) / g_setCurrent) * 100.0; // Turn the current error between set and measured into a percentage so it can be adjusted
  } else {
    g_adjustedCurrent = g_setCurrent;
  }  
  g_roundedMeasuredCurrent = round(g_measuredCurrent * 1000) / 1000.000; // This the best way I can think of rounding a floating point number to 3 decimal places.
  //only adjust the current of the set and meausred currents are diferent.  
  if (g_roundedMeasuredCurrent != g_setCurrent) {
  float senseResistor = 0.1;
  float voltageRatio = 2.5;
  float voltageReference = 2.048;
  int dacResolution = 4096;
  
  int dacVoltage = ((g_adjustedCurrent * senseResistor * voltageRatio)/voltageReference) * dacResolution;  
  // Send the value to the DAC.  
  setDac(dacVoltage,k_dacCurrentSet);  
  }
}

// Function to read heat sink temp
int readTemp() {
  float tempSensor1 = readAdc(k_adcTempSense1);
  float tempSensor2 = readAdc(k_adcTempSense2);
  float tempVoltage = ((tempSensor1 + tempSensor2) / 2) * 1000; //This takes an average of bothe temp sensors and converts the value to millivolts
  int temp = ((tempVoltage - 1885) / -11.2692307) + 20; //This comes from the datasheet to calculate the temp from the voltage given.
  return temp;
}

//Function to set the fan speed depending on the heatsink temprature.
void setFanSpeed () {
  int heatSinkTemp = readTemp();
  if (heatSinkTemp <= 30) {
    setDac(0,k_dacFanSet);
  }
  if (heatSinkTemp > 30) {
    setDac(2000,k_dacFanSet);
  }
  if (heatSinkTemp > 40) {
    setDac(2250,k_dacFanSet);
  }
  if (heatSinkTemp > 50) {
    setDac(2500,k_dacFanSet);
  }
  if (heatSinkTemp > 60) {
    setDac(2750,k_dacFanSet);
  }
  if (heatSinkTemp > 70) {
    setDac(3000,k_dacFanSet);
  }
}

// Function to allow the display to update every 0.5 seconds.
boolean updateDisplay() {
  unsigned long now = millis();
  if (now - g_timeSinceLastDisplay >= 500) {
    g_timeSinceLastDisplay = now;
    return true;
  } else {
    return false;
  }
}

// Function to set the mode of operation, ie, constant current, resistance or power.
void setMode(int menuMode) {
   g_mode = menuMode;
}

/*
Function to update the LCD. Accepts the LCD display variable to set to values or menu.
This determines what is on the LCD, case 0 means it is in mormal mode and displays the values that everyting is set to
it also includes a less than symbol to show what mode it is in on the screen. In this mode it tests if 500ms has passed,
if so, it clears the LCD and updates all the values. If you press the button on the encoder it will activate the showmenu
function further down which will update the displaytype so it will then equal displayMenu, which equals a const int of 1, as set above
it will then satisfy case 1 below and update the LCD to the menu functions.
*/
void updateLCD(int displayType) {  
  switch (displayType) {
    case 0:
      if (updateDisplay()) {
        lcd.clear();
        lcd.print("Voltage =");
        lcd.setCursor(10,0);
        lcd.print(inputVoltage,3);
        lcd.print("V");        
        lcd.setCursor(0,1);
        lcd.print("Current =");
        lcd.setCursor(10,1);
        lcd.print(setCurrent,3);
        lcd.print("A");
        if (g_mode == k_currentMode) {
          lcd.setCursor(19,1);
          lcd.print("<");
        }
        lcd.setCursor(0,2);
        lcd.print("Resist. =");
        lcd.setCursor(10,2);
        lcd.print(setResistance,3);
        lcd.print(char(0xF4));
        if (g_mode == k_resistanceMode) {
          lcd.setCursor(19,2);
          lcd.print("<");
        }
        lcd.setCursor(0,3);
        lcd.print("Power   =");
        lcd.setCursor(10,3);
        lcd.print(setPower,3);
        lcd.print("W");
        if (g_mode == k_powerMode) {
          lcd.setCursor(19,3);
          lcd.print("<");
        }
      }
    break;
    case 1:
      int menuItems = 3;      
      if (g_encoderPos < 0) g_encoderPos = menuItems;
      if (g_encoderPos > menuItems) g_encoderPos = 0;      
      if (encoderMoved || updateDisplay()) {
        lcd.clear();
        lcd.setCursor(1,0);
        lcd.print("Please Select Mode");     
        lcd.setCursor(2,2);
        switch (g_encoderPos) {
          case 0:
            lcd.print("Current");
          break;
          case 1:
            lcd.print("Resistance");
          break;
          case 2:
            lcd.print("Power");
          break;
          case 3:
            lcd.print("Cancel");
          break;
        }
      }
    break;
  }
}

/*
This function is called when the encoder button is pressed. 
If the LCD is not displaying the menu, it must mean that we want the menu, so it changes the lcdDisplay to
display the menu, if we are in the menu it must mean that we want to select that mode, it then changes the mode,
and redisplays the values.

*/
void showMenu() {
  if(g_lcdDisplay != k_displayMenu) {
  g_lcdDisplay = k_displayMenu;
  g_encoderPos = g_mode;
  } else {
    int menuMode = g_encoderPos;
    switch (menuMode) {
      case 0:
        // Current Mode        
        setMode(k_currentMode);
      break;
      case 1:
        // Resistance Mode        
        setMode(k_resistanceMode);
      break;
      case 2:
        // Power Mode        
        setMode(k_powerMode);
      break;
      case 3:
        // Do Nothing, Cancel        
      break;
    }
  g_lcdDisplay = k_displayValues;
  }
}

//Function to read the ADC, accepts the channel to be read.
float readAdc(int channel) {
  byte adcPrimaryRegister = 0b00000110;      // Sets default Primary ADC Address register B00000110, This is a default address setting, the third LSB is set to one to start the ADC, the second LSB is to set the ADC to single ended mode, the LSB is for D2 address bit, for this ADC its a "Don't Care" bit.
  byte adcPrimaryRegisterMask = 0b00000111;  // b00000111 Isolates the three LSB. 
  byte adcPrimaryConfig = adcPrimaryRegister & adcPrimaryRegisterMask; // ensures the adc register is limited to the mask and assembles the configuration byte to send to ADC.
  byte adcSecondaryConfig = channel << 6;
  noInterrupts(); // disable interupts to prepare to send address data to the ADC.  
  digitalWrite(k_adcChipSelectPin,LOW); // take the Chip Select pin low to select the ADC.
  SPI.transfer(adcPrimaryConfig); //  send in the primary configuration address byte to the ADC.  
  byte adcPrimaryByte = SPI.transfer(adcSecondaryConfig); // read the primary byte, also sending in the secondary address byte.  
  byte adcSecondaryByte = SPI.transfer(0x00); // read the secondary byte, also sending 0 as this doesn't matter. 
  digitalWrite(k_adcChipSelectPin,HIGH); // take the Chip Select pin high to de-select the ADC.
  interrupts(); // Enable interupts.
  byte adcPrimaryByteMask = 0b00001111;      // b00001111 isolates the 4 LSB for the value returned. 
  adcPrimaryByte &= adcPrimaryByteMask; // Limits the value of the primary byte to the 4 LSB:
  int digitalValue = (adcPrimaryByte << 8) | adcSecondaryByte; // Shifts the 4 LSB of the primary byte to become the 4 MSB of the 12 bit digital value, this is then ORed to the secondary byte value that holds the 8 LSB of the digital value.
  float value = (float(digitalValue) * 2.048) / 4096.000; // The digital value is converted to an analogue voltage using a VREF of 2.048V.
  return value; // Returns the value from the function
}

// Function to set the DAC, Accepts the Value to be sent and the cannel of the DAC to be used.
void setDac(int value, int channel) {
  byte dacRegister = 0b00110000; // Sets default DAC registers B00110000, 1st bit choses DAC, A=0 B=1, 2nd Bit bypasses input Buffer, 3rd bit sets output gain to 1x, 4th bit controls active low shutdown. LSB are insignifigant here.
  int dacSecondaryByteMask = 0xFF; // Isolates the last 8 bits of the 12 bit value, B0000000011111111.
  byte dacPrimaryByte = (value >> 8) | dacRegister; //Value is a maximum 12 Bit value, it is shifted to the right by 8 bytes to get the first 4 MSB out of the value for entry into th Primary Byte, then ORed with the dacRegister  
  byte dacSecondaryByte = value & dacSecondaryByteMask; // compares the 12 bit value to isolate the 8 LSB and reduce it to a single byte. 
  // Sets the MSB in the primaryByte to determine the DAC to be set, DAC A=0, DAC B=1
  switch (channel) {
   case 0:
     dacPrimaryByte &= ~(1 << 7);     
   break;
   case 1:
     dacPrimaryByte |= (1 << 7);  
  }
  noInterrupts(); // disable interupts to prepare to send data to the DAC
  digitalWrite(k_dacChipSelectPin,LOW); // take the Chip Select pin low to select the DAC:
  SPI.transfer(dacPrimaryByte); //  send in the Primary Byte:
  SPI.transfer(dacSecondaryByte);// send in the Secondary Byte
  digitalWrite(k_dacChipSelectPin,HIGH);// take the Chip Select pin high to de-select the DAC:
  interrupts(); // Enable interupts
}
