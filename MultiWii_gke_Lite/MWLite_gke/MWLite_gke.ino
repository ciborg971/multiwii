
/*

 MWLite_gke
 May 2013
 
 MWLite_gke is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 any later version. see <http://www.gnu.org/licenses/>
 
 MWLite_gke is distributed in the hope that it will be useful,but WITHOUT ANY 
 WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR 
 A PARTICULAR PURPOSE. 
 
 See the GNU General Public License for more details.
 
 Lite was based originally on MultiWiiCopter V2.2 by Alexandre Dubus
 www.multiwii.com, March  2013. The rewrite by Prof Greg Egan was renamed 
 so as not to confuse it with the original.
 
 It preserves the parameter specification and GUI interface with parameters
 scaled to familiar values. 
 
 Major changes include the control core which comes from UAVX with the 
 addition of MW modes.
 
 Lite supports only Atmel 32u4 processors using an MPU6050 and optionally 
 BMP085 and MS5611 barometers and HMC5883 magnetometer with 4 motors, 
 no servos and 8KHz PWM for brushed DC motors.
 
 */

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "def.h"

static boolean  rcOptions[CHECKBOX_ITEMS];

static uint32_t NowuS;
static uint32_t previousCycleuS = 0;
static uint32_t nextCycleuS = 0;
static uint16_t cycleTime = 0;
static int16_t  i2cErrors = 0;
static boolean slotFree = true;

static int16_t  debug[4];

// Tuning

static boolean tuningAxis[3] = {
  false, false, false};
static int16_t tuneStimulus[3];
static uint8_t tuneaxis = PITCH;

boolean tuningAlt = false;
static int32_t tuneAltStimulus;

// RC

static int16_t rcData[RC_CHANS];    // interval [1000;2000]
static int16_t rcCommand[RC_CHANS]; // interval [1000;2000] for THROTTLE and [-500;+500] for ROLL/PITCH/YAW 
static uint16_t thrCurve[33];// lookup table for expo & mid THROTTLE

static boolean rcNewValues = false;
static boolean inFailsafe = false;

// IMU

#define  GRAVITY    4096L // asin(1/GRAVITY) = 0.1 deg - OK
#define  GRAVITY_SQ  (GRAVITY * GRAVITY)
#define  GRAVITY_R  (1.0/(float)GRAVITY)
#define  ACC_25DEG  ((int16_t)(GRAVITY * 0.423));

static int16_t angle[3]    = {
  0,0};  // 0.1deg

static int16_t  gyroADC[3], accADC[3], magADC[3];
static int16_t accData[3], gyroData[3];
static uint16_t calibratingA = 0;  
static uint16_t calibratingG;
static float YawAngle = 0.0f;
static int16_t Heading = 0;
static int16_t holdHeading = 0;
static int16_t headfreeHeading;
static float velZ = 0.0;
static int16_t accZ = 0;

// Baro

boolean calibratingB = true;
static int32_t  relativeAltitude;  // in cm
static int16_t  ROC; // variometer in cm/s
static int32_t densityAltitude = 0; // cm
static int16_t AltitudeIntE = 0;
static int32_t desiredAltitude;
static int16_t altPID;
static uint16_t hoverThrottle = 1300;

// PID

int16_t RateIntE[3] = {
  0,0,0};
int16_t AngleIntE[2] = {
  0,0};

// Motors

static int16_t axisPID[3];
static int16_t motor[NUMBER_MOTOR];

// EEPROM

static uint8_t dynP8[3], dynI8[3], dynD8[3];

static struct {
  int16_t accZero[3];
  int16_t gyroZero[3];
  int16_t magZero[3];
  float RelayK[3], RelayP[3];
  boolean accCalibrated, magCalibrated;
  uint8_t checksum; // must be last! 
} 
global_conf;

static struct {
  uint16_t thisVersion;
  uint8_t P8[PIDITEMS], I8[PIDITEMS], D8[PIDITEMS];
  uint8_t rcRate8;
  uint8_t rcExpo8;
  uint8_t rollPitchRate;
  uint8_t yawRate;
  uint8_t dynThrPID;
  uint8_t thrMid8;
  uint8_t thrExpo8;
  uint16_t activate[CHECKBOX_ITEMS];
  uint16_t cycletimeuS;
  int16_t minthrottle;
  uint8_t  checksum; // must be last! 
} 
conf;

// Battery

static struct {
  uint8_t  vbat;               // battery voltage in 0.1V steps
  uint16_t intPowerMeterSum;
  uint16_t rssi;               // range: [0;1023]
} 
analog;

static int16_t throttleLVCScale = 1024;

void annexCode() {
  static uint32_t alarmUpdatemS = 0;
  uint32_t NowmS; 

  checkBattery();

  if (f.ALARM) {
    NowmS = millis();
    if( NowmS > alarmUpdatemS) {
      alarmUpdatemS = NowmS + 250;
      LED_BLUE_TOGGLE;    
    }
  } 
  else  
    if (f.ARMED) {
    LED_BLUE_ON;
  } 
  else
    LED_BLUE_OFF;

  if (slotFree)
    serialCom(); // also blinks LED

} // annexCode


void setup() { 

  SerialOpen(0,SERIAL0_COM_SPEED);
  SerialOpen(1,SERIAL1_COM_SPEED);

  LED_BLUE_PINMODE;
  initOutput();

#if defined(SPEKTRUM)
  checkSpektrumBinding();
#endif

  softi2cPower();

  //  LoadDefaults();
  //  writeParams(1);

  delay(100);

  memset(&f, 0, sizeof(f)); // set all flags false

  readEEPROM();
  readGlobalSet();
  readEEPROM(); // load current setting data

  blinkLED(50, 1);          
  configureReceiver();
  initSensors();

  calibratingG = 0;
  calibratingA = 0;
  calibratingB = 32;

  ADCSRA |= _BV(ADPS2) ; 
  ADCSRA &= ~_BV(ADPS1); 
  ADCSRA &= ~_BV(ADPS0); // http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1208715493/11

  zeroIntegrals();
  relativeAltitude = ROC = 0;
  previousCycleuS = micros();

} // setup


void loop (void) {

  cycleTime = micros() - previousCycleuS;

  while(micros() < nextCycleuS) {
  }; // wait

  slotFree = true;

  previousCycleuS = micros();
  nextCycleuS = previousCycleuS + conf.cycletimeuS;

  getRatesAndAccelerations();
  getEstimatedAttitude();  
  computeAltitudeControl();
  updateMagnetometer();

  computeControl();
  mixTable();
  writeMotors();

  if (newRCValues()){ 
    getRCInput(); 
    doConfigUpdate();    
    doRCRates(); 
    doMagHold();
    doHeadFree();
    doAltitudeControl(); // potentially overides throttle 
  }

  annexCode();

} // loop































