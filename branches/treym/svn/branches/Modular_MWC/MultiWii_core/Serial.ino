// 256 RX buffer is needed for GPS communication (64 or 128 was too short)
// it avoids also modulo operations
#define MSP_IDENT                100   //out message         multitype + version
#define MSP_STATUS               101   //out message         cycletime & errors_count & sensor present & box activation
#define MSP_RAW_IMU              102   //out message         9 DOF
#define MSP_SERVO                103   //out message         8 servos
#define MSP_MOTOR                104   //out message         8 motors
#define MSP_RC                   105   //out message         8 rc chan
#define MSP_RAW_GPS              106   //out message         fix, numsat, lat, lon, alt, speed
#define MSP_COMP_GPS             107   //out message         distance home, direction home
#define MSP_ATTITUDE             108   //out message         2 angles 1 heading
#define MSP_ALTITUDE             109   //out message         1 altitude
#define MSP_BAT                  110   //out message         vbat, powermetersum
#define MSP_RC_TUNING            111   //out message         rc rate, rc expo, rollpitch rate, yaw rate, dyn throttle PID
#define MSP_PID                  112   //out message         up to 16 P I D (8 are used)
#define MSP_BOX                  113   //out message         up to 16 checkbox (11 are used)
#define MSP_MISC                 114   //out message         powermeter trig + 8 free for future use

#define MSP_SET_RAW_RC           200   //in message          8 rc chan
#define MSP_SET_RAW_GPS          201   //in message          fix, numsat, lat, lon, alt, speed
#define MSP_SET_PID              202   //in message          up to 16 P I D (8 are used)
#define MSP_SET_BOX              203   //in message          up to 16 checkbox (11 are used)
#define MSP_SET_RC_TUNING        204   //in message          rc rate, rc expo, rollpitch rate, yaw rate, dyn throttle PID
#define MSP_ACC_CALIBRATION      205   //in message          no param
#define MSP_MAG_CALIBRATION      206   //in message          no param
#define MSP_SET_MISC             207   //in message          powermeter trig + 8 free for future use
#define MSP_RESET_CONF           208   //in message          no param

#define MSP_EEPROM_WRITE         250   //in message          no param

#define MSP_DEBUG                254   //out message         debug1,debug2,debug3,debug4


static uint8_t checksum,stateMSP,indRX,inBuf[64];

uint32_t read32() {
  uint32_t t = inBuf[indRX++];
  t+= inBuf[indRX++]<<8;
  t+= inBuf[indRX++]<<16;
  t+= inBuf[indRX++]<<24;
  return t;
}

uint16_t read16() {
  uint16_t t = inBuf[indRX++];
  t+= inBuf[indRX++]<<8;
  return t;
}
uint8_t read8()  {return inBuf[indRX++]&0xff;}

void headSerialReply(uint8_t c,uint8_t s) {
  serialize8('$');serialize8('M');serialize8('>');serialize8(s);serialize8(c);checksum = 0;
}

void tailSerialReply() {
  serialize8(checksum);UartSendData();
}

void serialCom() {
  uint8_t i, c;
  static uint8_t offset,dataSize;
  
  while (SerialAvailable(0)) {
    c = SerialRead(0);

    if (stateMSP > 99) {                           // a message with a length indication, indicating a non null payload
      if (offset <= dataSize) {                    // there are still some octets to read (including checksum) to complete a full message
        if (offset < dataSize) checksum ^= c;      // the checksum is computed, except for the last octet
        inBuf[offset++] = c;
      } else {                                     // we have read all the payload
        if ( checksum == inBuf[dataSize] ) {       // we check is the computed checksum is ok
          switch(stateMSP) {                       // if yes, then we execute different code depending on the message code. read8/16/32 will look into the inBuf buffer
            case MSP_SET_RAW_RC:
              for(i=0;i<8;i++) {rcData[i] = read16();} break;
            case MSP_SET_RAW_GPS:
              /*
              GPS_fix = read8();
              GPS_numSat = read8();
              GPS_latitude = read32();
              GPS_longitude = read32();
              GPS_altitude = read16();
              GPS_speed = read16();
              GPS_update = 1; break;
              */
            case MSP_SET_PID:
              for(i=0;i<PIDITEMS;i++) {P8[i]=read8();I8[i]=read8();D8[i]=read8();} break;
            case MSP_SET_BOX:
              for(i=0;i<CHECKBOXITEMS;i++) {activate[i]=read16();} break;
            case MSP_SET_RC_TUNING:
              rcRate8 = read8();
              rcExpo8 = read8();
              rollPitchRate = read8();
              yawRate = read8();
              dynThrPID = read8(); break;
            case MSP_SET_MISC:
              #if defined(POWERMETER)
                powerTrigger1 = read16() / PLEVELSCALE; // we rely on writeParams() to compute corresponding pAlarm value
              #endif
              break;
          }
        }
        stateMSP = 0;                              // in any case we reset the MSP state
      }
    }

    if (stateMSP < 5) {
      if (stateMSP == 4) {                           // this protocol state indicates we have a message with a lenght indication, and we read here the message code (fifth octet) 
        if (c > 99) {                                // we check if it's a valid code (should be >99)
          stateMSP = c;                              // the message code is then reuse to feed the protocol state
          offset = 0;checksum = 0;indRX=0;           // and we init some values which will be used in the next loops to grasp the payload
        } else {
          stateMSP = 0;                              // the message code seems to be invalid. this should not happen => we reset the protocol state
        }
      }
      if (stateMSP == 3) {                           // here, we need to check if the fourth octet indicates a code indication (>99) or a payload lenght indication (<100)
        if (c<100) {                                 // a message with a length indication, indicating a non null payload
          stateMSP++;                                // we update the protocol state to read the next octet
          dataSize = c;                              // we store the payload lenght
          if (dataSize>63) dataSize=63;              // in order to avoid overflow, we limit the size. this should not happen
        } else {
          switch(c) {                                // if we are here, the fourth octet indicates a code message
            case MSP_IDENT:                          // and we check message code to execute the relative code
              headSerialReply(c,2);                  // we reply with an header indicating a payload lenght of 2 octets
              serialize8(VERSION);                   // the first octet. serialize8/16/32 is used also to compute a checksum
              serialize8(MULTITYPE);                 // the second one
              tailSerialReply();break;               // mainly to send the last octet which is the checksum
            case MSP_STATUS:
              headSerialReply(c,8);
              serialize16(cycleTime);
              serialize16(i2c_errors_count);
              serialize16(ACC|BARO<<1|MAG<<2|GPS<<3|SONAR<<4);
              serialize16(accMode<<BOXACC|/*baroMode*/0<<BOXBARO|/*magMode*/0<<BOXMAG|armed<<BOXARM|
                          /*GPSModeHome*/0<<BOXGPSHOME|/*GPSModeHold*/0<</*BOXGPSHOLD*/0|/*headFreeMode*/0<<BOXHEADFREE);
              tailSerialReply();break;
            case MSP_RAW_IMU:
              headSerialReply(c,18);
              for(i=0;i<3;i++) serialize16(accSmooth[i]);
              for(i=0;i<3;i++) serialize16(gyroData[i]);
              for(i=0;i<3;i++) serialize16(magADC[i]);
              tailSerialReply();break;
            case MSP_SERVO:
              headSerialReply(c,16);
              for(i=0;i<8;i++) serialize16(servo[i]);
              tailSerialReply();break;
            case MSP_MOTOR:
              headSerialReply(c,16);
              for(i=0;i<8;i++) serialize16(motor[i]);
              tailSerialReply();break;
            case MSP_RC:
              headSerialReply(c,16);
              for(i=0;i<8;i++) serialize16(rcData[i]);
              tailSerialReply();break;
            case MSP_RAW_GPS:
              headSerialReply(c,14);
              serialize8(/*GPS_fix*/0);
              serialize8(/*GPS_numSat*/0);
              serialize32(/*GPS_latitude*/0);
              serialize32(/*GPS_longitude*/0);
              serialize16(/*GPS_altitude*/0);
              serialize16(/*GPS_speed*/0);
              tailSerialReply();break;
            case MSP_COMP_GPS:
              headSerialReply(c,5);
              serialize16(/*GPS_distanceToHome*/0);
              serialize16(/*GPS_directionToHome+180*/0);
              serialize8(/*GPS_update*/0);
              tailSerialReply();break;
            case MSP_ATTITUDE:
              headSerialReply(c,6);
              for(i=0;i<2;i++) serialize16(angle[i]);
              serialize16(/*heading*/0);
              tailSerialReply();break;
            case MSP_ALTITUDE:
              headSerialReply(c,4);
              serialize32(/*EstAlt*/0);
              tailSerialReply();break;
            case MSP_BAT:
              headSerialReply(c,3);
              serialize8(/*vbat*/0);
              serialize16(/*intPowerMeterSum*/0);
              tailSerialReply();break;
            case MSP_RC_TUNING:
              headSerialReply(c,5);
              serialize8(rcRate8);
              serialize8(rcExpo8);
              serialize8(rollPitchRate);
              serialize8(yawRate);
              serialize8(dynThrPID);
              tailSerialReply();break;
            case MSP_PID:
              headSerialReply(c,3*PIDITEMS);
              for(i=0;i<PIDITEMS;i++)    {serialize8(P8[i]);serialize8(I8[i]);serialize8(D8[i]);}
              tailSerialReply();break;
            case MSP_BOX:
              headSerialReply(c,2*CHECKBOXITEMS);
              for(i=0;i<CHECKBOXITEMS;i++)    {serialize16(activate[i]);}
              tailSerialReply();break;
            case MSP_MISC:
              headSerialReply(c,2);
              serialize16(/*intPowerTrigger1*/0);
              tailSerialReply();break;
            case MSP_RESET_CONF:
              checkNewConf++;checkFirstTime();
              checkNewConf--;checkFirstTime();break;
            case MSP_ACC_CALIBRATION:
              calibratingA=400;break;
            case MSP_MAG_CALIBRATION:
              /*calibratingM=1;break;*/
            case MSP_EEPROM_WRITE:
              writeParams(0);break;
            case MSP_DEBUG:
              headSerialReply(c,8);
              serialize16(debug1); // 4 variables are here for general monitoring purpose
              serialize16(debug2);
              serialize16(debug3);
              serialize16(debug4);
              tailSerialReply();break;
          }
          stateMSP=0;                              // we reset the protocol state for the next loop
        }
      } else {
        switch(c) {                                // header detection $MW<
          case '$':
            if (stateMSP == 0) stateMSP++; break;  // first octet ok, no need to go further
          case 'M':
            if (stateMSP == 1) stateMSP++; break;  // second octet ok, no need to go further
          case '<':
            if (stateMSP == 2) stateMSP++; break;  // third octet ok, no need to go further
        }
      }
    }
  }
}

// *******************************************************
// Interrupt driven UART transmitter - using a ring buffer
// *******************************************************

void serialize32(uint32_t a) {
  static uint8_t t;
  t = a;       bufTX[headTX++] = t ; checksum ^= t;
  t = a>>8;    bufTX[headTX++] = t ; checksum ^= t;
  t = a>>16;   bufTX[headTX++] = t ; checksum ^= t;
  t = a>>24;   bufTX[headTX++] = t ; checksum ^= t;
  #if !defined(USB_OB)
    UCSR0B |= (1<<UDRIE0);      // in case ISR_UART desactivates the interrupt, we force its reactivation anyway
  #endif 
}
void serialize16(int16_t a) {
  static uint8_t t;
  t = a;          bufTX[headTX++] = t ; checksum ^= t;
  t = a>>8&0xff;  bufTX[headTX++] = t ; checksum ^= t;
  #if !defined(USB_OB)
    UCSR0B |= (1<<UDRIE0);
  #endif 
}
void serialize8(uint8_t a)  {
  bufTX[headTX++]  = a; checksum ^= a;
  #if !defined(USB_OB)
    UCSR0B |= (1<<UDRIE0);
  #endif
}

