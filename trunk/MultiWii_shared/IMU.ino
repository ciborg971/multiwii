
void computeIMU () {
  uint8_t axis;
  static int16_t gyroADCprevious[3] = {0,0,0};
  int16_t gyroADCp[3];
  int16_t gyroADCinter[3];
  static uint32_t timeInterleave = 0;

  //we separate the 2 situations because reading gyro values with a gyro only setup can be acchieved at a higher rate
  //gyro+nunchuk: we must wait for a quite high delay betwwen 2 reads to get both WM+ and Nunchuk data. It works with 3ms
  //gyro only: the delay to read 2 consecutive values can be reduced to only 0.65ms
  #if defined(NUNCHUCK)
    annexCode();
    while((micros()-timeInterleave)<INTERLEAVING_DELAY) ; //interleaving delay between 2 consecutive reads
    timeInterleave=micros();
    ACC_getADC();
    getEstimatedAttitude(); // computation time must last less than one interleaving delay
    while((micros()-timeInterleave)<INTERLEAVING_DELAY) ; //interleaving delay between 2 consecutive reads
    timeInterleave=micros();
    f.NUNCHUKDATA = 1;
    while(f.NUNCHUKDATA) ACC_getADC(); // For this interleaving reading, we must have a gyro update at this point (less delay)

    for (axis = 0; axis < 3; axis++) {
      // empirical, we take a weighted value of the current and the previous values
      // /4 is to average 4 values, note: overflow is not possible for WMP gyro here
      gyroData[axis] = (gyroADC[axis]*3+gyroADCprevious[axis])/4;
      gyroADCprevious[axis] = gyroADC[axis];
    }
  #else
    #if ACC
      ACC_getADC();
      getEstimatedAttitude();
    #endif
    #if GYRO
      Gyro_getADC();
    #endif
    for (axis = 0; axis < 3; axis++)
      gyroADCp[axis] =  gyroADC[axis];
    timeInterleave=micros();
    annexCode();
    if ((micros()-timeInterleave)>650) {
       annex650_overrun_count++;
    } else {
       while((micros()-timeInterleave)<650) ; //empirical, interleaving delay between 2 consecutive reads
    }
    #if GYRO
      Gyro_getADC();
    #endif
    for (axis = 0; axis < 3; axis++) {
      gyroADCinter[axis] =  gyroADC[axis]+gyroADCp[axis];
      // empirical, we take a weighted value of the current and the previous values
      gyroData[axis] = (gyroADCinter[axis]+gyroADCprevious[axis])/3;
      gyroADCprevious[axis] = gyroADCinter[axis]/2;
      if (!ACC) accADC[axis]=0;
    }
  #endif
  #if defined(GYRO_SMOOTHING)
    static int16_t gyroSmooth[3] = {0,0,0};
    for (axis = 0; axis < 3; axis++) {
      gyroData[axis] = (int16_t) ( ( (int32_t)((int32_t)gyroSmooth[axis] * (conf.Smoothing[axis]-1) )+gyroData[axis]+1 ) / conf.Smoothing[axis]);
      gyroSmooth[axis] = gyroData[axis];
    }
  #elif defined(TRI)
    static int16_t gyroYawSmooth = 0;
    gyroData[YAW] = (gyroYawSmooth*2+gyroData[YAW])/3;
    gyroYawSmooth = gyroData[YAW];
  #endif
}

// **************************************************
// Simplified IMU based on "Complementary Filter"
// Inspired by http://starlino.com/imu_guide.html
//
// adapted by ziss_dm : http://www.multiwii.com/forum/viewtopic.php?f=8&t=198
//
// The following ideas was used in this project:
// 1) Rotation matrix: http://en.wikipedia.org/wiki/Rotation_matrix
// 2) Small-angle approximation: http://en.wikipedia.org/wiki/Small-angle_approximation
// 3) C. Hastings approximation for atan2()
// 4) Optimization tricks: http://www.hackersdelight.org/
//
// Currently Magnetometer uses separate CF which is used only
// for heading approximation.
//
// **************************************************

//******  advanced users settings *******************
/* Set the Low Pass Filter factor for ACC
   Increasing this value would reduce ACC noise (visible in GUI), but would increase ACC lag time
   Comment this if  you do not want filter at all.
   unit = n power of 2 */
// this one is also used for ALT HOLD calculation, should not be changed
#define ACC_LPF_FACTOR 4 // that means a LPF of 16

/* Set the Gyro Weight for Gyro/Acc complementary filter
   Increasing this value would reduce and delay Acc influence on the output of the filter
   unit = n power of 2 */
#ifndef GYR_CMPF_FACTOR
  #define GYR_CMPF_FACTOR 9 // -> 512 (was 400)
#endif

/* Set the Gyro Weight for Gyro/Magnetometer complementary filter
   Increasing this value would reduce and delay Magnetometer influence on the output of the filter
   unit = n power of 2 */
#ifndef GYR_CMPFM_FACTOR
  #define GYR_CMPFM_FACTOR 8 // -> 256 (was 200)
#endif

/* the RESOLUTION factor increases the ACC&MAG data length so that the fractional part
   could be calculated in matrix rotation.
   If too high, the computation could overflow
   ACC & MAG length + RESOLUTION should not exceed 16bit for a safe computation
   (multiplication of 16 x 16 = 32 bits)*/
#define RESOLUTION 4 //it's compatible with 12 bits resolution sensor values

//****** end of advanced users settings *************

#define GYRO_SCALE ((2279 * PI)/((32767.0f / 4.0f ) * 180.0f * 1000000.0f)) //(ITG3200 and MPU6050)
// +-2000/sec deg scale
// for WMP, empirical value should be #define GYRO_SCALE (1.0f/200e6f)
// !!!!should be adjusted to the rad/sec and be part defined in each gyro sensor

typedef struct int32_t_vector {
  int32_t X,Y,Z;
} t_int32_t_vector_def;

typedef union {
  int32_t A[3];
  t_int32_t_vector_def V;
} t_int32_t_vector;

int16_t _atan2(float y, float x){
  #define fp_is_neg(val) ((((uint8_t*)&val)[3] & 0x80) != 0)
  float z = y / x;
  int16_t zi = abs(int16_t(z * 100)); 
  int8_t y_neg = fp_is_neg(y);
  if ( zi < 100 ){
    if (zi > 10) 
     z = z / (1.0f + 0.28f * z * z);
   if (fp_is_neg(x)) {
     if (y_neg) z -= PI;
     else z += PI;
   }
  } else {
   z = (PI / 2.0f) - z / (z * z + 0.28f);
   if (y_neg) z -= PI;
  }
  return z* (1800.0f / PI);
}

float InvSqrt (float x){ 
  union{  
    int32_t i;  
    float   f; 
  } conv; 
  conv.f = x; 
  conv.i = 0x5f3759df - (conv.i >> 1); 
  return 0.5f * conv.f * (3.0f - x * conv.f * conv.f);
}

// Rotate Estimated vector(s) with small angle approximation, according to the gyro data
// still possible to optimize
void rotateV32(struct int32_t_vector *v,float* delta) {
  int32_t_vector v_tmp = *v;
  v->Z -= delta[ROLL]  * v_tmp.X + delta[PITCH] * v_tmp.Y;
  v->X += delta[ROLL]  * v_tmp.Z - delta[YAW]   * v_tmp.Y;
  v->Y += delta[PITCH] * v_tmp.Z + delta[YAW]   * v_tmp.X; 
}

static int32_t accLPF32[3]    = {0, 0, 1};
static float invG; // 1/|G|

static t_int32_t_vector EstG32;
#if MAG
  static t_int32_t_vector EstM32;
#endif

void getEstimatedAttitude(){
  uint8_t axis;
  int32_t accMag = 0 , CMP;
  float scale, deltaGyroAngle[3];
  static uint16_t previousT;
  uint16_t currentT = micros();

  scale = (currentT - previousT) * GYRO_SCALE;
  previousT = currentT;

  // Initialization
  for (axis = 0; axis < 3; axis++) {
    deltaGyroAngle[axis] = gyroADC[axis]  * scale;

    accLPF32[axis]    -= accLPF32[axis]>>ACC_LPF_FACTOR;
    accLPF32[axis]    += accADC[axis];
    accSmooth[axis]    = accLPF32[axis]>>ACC_LPF_FACTOR;

    accMag += (int32_t)accSmooth[axis]*accSmooth[axis] ;
  }
  accMag = accMag*100/((int32_t)acc_1G*acc_1G);

  rotateV32(&EstG32.V,deltaGyroAngle);
  #if MAG
    rotateV32(&EstM32.V,deltaGyroAngle);
  #endif

  if ( abs(accSmooth[ROLL])<acc_25deg && abs(accSmooth[PITCH])<acc_25deg && accSmooth[YAW]>0) {
    f.SMALL_ANGLES_25 = 1;
  } else {
    f.SMALL_ANGLES_25 = 0;
  }

  // Apply complimentary filter (Gyro drift correction)
  // If accel magnitude >1.4G or <0.6G and ACC vector outside of the limit range => we neutralize the effect of accelerometers in the angle estimation.
  // To do that, we just skip filter, as EstV already rotated by Gyro
  if ( ( 36 < accMag && accMag < 196 ) || f.SMALL_ANGLES_25 )
    for (axis = 0; axis < 3; axis++) {
      CMP  = EstG32.A[axis]<<GYR_CMPF_FACTOR;
      CMP -= EstG32.A[axis];
      CMP += ((int32_t)accSmooth[axis])<<RESOLUTION;
      EstG32.A[axis] = CMP>>GYR_CMPF_FACTOR;
    }
  #if MAG
    for (axis = 0; axis < 3; axis++) {
      CMP  = EstM32.A[axis]<<GYR_CMPFM_FACTOR;
      CMP -= EstM32.A[axis];
      CMP += ((int32_t)magADC[axis])<<RESOLUTION;
      EstM32.A[axis] = CMP>>GYR_CMPFM_FACTOR;
    }
  #endif
  
  // Attitude of the estimated vector
  int32_t sqGZ = sq(EstG32.V.Z);
  int32_t sqGX = sq(EstG32.V.X);
  int32_t sqGY = sq(EstG32.V.Y);
  int32_t sqGX_sqGZ = sqGX + sqGZ;
  float invmagXZ  = InvSqrt(sqGX_sqGZ);
  invG = InvSqrt(sqGX_sqGZ + sqGY);
  angle[ROLL]  = _atan2(EstG32.V.X , EstG32.V.Z);
  angle[PITCH] = _atan2(EstG32.V.Y*invmagXZ , 1.0);

  #if MAG
    heading = _atan2(
      EstM32.V.Z * EstG32.V.X - EstM32.V.X * EstG32.V.Z,
      EstM32.V.Y * invG * sqGX_sqGZ  - (EstM32.V.X * EstG32.V.X + EstM32.V.Z * EstG32.V.Z) * invG * EstG32.V.Y ); 
    heading += MAG_DECLINIATION * 10; //add declination
    heading = heading /10;
  #endif
}

#define UPDATE_INTERVAL 25000    // 40hz update rate (20hz LPF on acc)
#define BARO_TAB_SIZE   21

#define ACC_Z_DEADBAND (acc_1G/40)

#define applyDeadband(value, deadband)  \
  if(abs(value) < deadband) {           \
    value = 0;                          \
  } else if(value > 0){                 \
    value -= deadband;                  \
  } else if(value < 0){                 \
    value += deadband;                  \
  }

#if BARO
uint8_t getEstimatedAltitude(){
  static uint32_t deadLine;
  static int32_t baroGroundPressure;

  uint16_t dTime = currentTime - deadLine;
  if (dTime < UPDATE_INTERVAL) return 0;
  deadLine = currentTime;

  if(calibratingB > 0) {
    baroGroundPressure = baroPressureSum/(BARO_TAB_SIZE - 1);
    calibratingB--;
  }
  
  // pressure relative to ground pressure with temperature compensation (fast!)
  // baroGroundPressure is not supposed to be 0 here
  // see: https://code.google.com/p/ardupilot-mega/source/browse/libraries/AP_Baro/AP_Baro.cpp
  BaroAlt = log( baroGroundPressure / (baroPressureSum/(float)(BARO_TAB_SIZE - 1)) ) * (baroTemperature+27315) * 29.271267f; // in cemtimeter 


  EstAlt = (EstAlt * 6 + BaroAlt * 2) >> 3; // additional LPF to reduce baro noise (faster by 30 µs)

  #if (defined(VARIOMETER) && (VARIOMETER != 2)) || !defined(SUPPRESS_BARO_ALTHOLD)
    //P
    int16_t error16 = constrain(AltHold - EstAlt, -300, 300);
    applyDeadband(error16, 10); //remove small P parametr to reduce noise near zero position
    BaroPID = constrain((conf.P8[PIDALT] * error16 / 100), -150, +150);
    
    //I
    errorAltitudeI += error16 * conf.I8[PIDALT]/50;
    errorAltitudeI = constrain(errorAltitudeI,-30000,30000);
    BaroPID += (errorAltitudeI / 500); //I in range +/-60
    
    
    // projection of ACC vector to global Z, with 1G subtructed
    // Math: accZ = A * G / |G| - 1G
    int16_t accZ = (accSmooth[ROLL] * EstG32.V.X + accSmooth[PITCH] * EstG32.V.Y + accSmooth[YAW] * EstG32.V.Z) * invG;

    static int16_t accZoffset = 0; // = acc_1G*6; //58 bytes saved and convergence is fast enough to omit init
    if (!f.ARMED) {
      accZoffset -= accZoffset/6;
      accZoffset += accZ;
    }  
    accZ -= accZoffset/6;
    applyDeadband(accZ, ACC_Z_DEADBAND);
    
    static float vel = 0.0f;
    static float accVelScale = 9.80665f / 10000.0f / acc_1G ;
    
    // Integrator - velocity, cm/sec
    vel += accZ * accVelScale * dTime;
    
    static int32_t lastBaroAlt;
    int16_t baroVel = (EstAlt - lastBaroAlt) * 1000000.0f / dTime;
    lastBaroAlt = EstAlt;
  
    baroVel = constrain(baroVel, -300, 300); // constrain baro velocity +/- 300cm/s
    applyDeadband(baroVel, 10); // to reduce noise near zero
    
    // apply Complimentary Filter to keep the calculated velocity based on baro velocity (i.e. near real velocity). 
    // By using CF it's possible to correct the drift of integrated accZ (velocity) without loosing the phase, i.e without delay
    vel = vel * 0.985f + baroVel * 0.015f;
    
    //D
    int16_t vel_tmp = vel;
    applyDeadband(vel_tmp, 5);
    vario = vel_tmp;
    BaroPID -= constrain(conf.D8[PIDALT] * vel_tmp / 20, -150, 150);
  #endif
  return 1;
}
#endif //BARO

