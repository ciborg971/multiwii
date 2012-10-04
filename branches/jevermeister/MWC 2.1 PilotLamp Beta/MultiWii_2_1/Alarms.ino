static uint8_t cycle_Done[5]={0,0,0,0,0}, 
               channelIsOn[5] = {0,0,0,0,0};
static uint32_t channelLastToggleTime[5] ={0,0,0,0,0};
#if defined(BUZZER)
  static uint8_t beeperOnBox = 0,
                 warn_noGPSfix = 0,
                 warn_failsafe = 0, 
                 warn_runtime = 0,
                 warn_vbat = 0,
                 buzzerSequenceActive=0;
  uint8_t isBuzzerON() { return channelIsOn[1]; } // returns true while buzzer is buzzing; returns 0 for silent periods

/********************************************************************/
/****                      Alarm Handling                        ****/
/********************************************************************/
  void alarmHandler(){
    
    #if defined(VBAT)
      if ( ( (vbat>VBATLEVEL1_3S) 
      #if defined(POWERMETER)
                           && ( (pMeter[PMOTOR_SUM] < pAlarm) || (pAlarm == 0) )
      #endif
                         )  || (NO_VBAT>vbat)                              ) // ToLuSe
      {                                          // VBAT ok AND powermeter ok, alarm off
        warn_vbat = 0;
      #if defined(POWERMETER)
      } else if (pMeter[PMOTOR_SUM] > pAlarm) {                             // sound alarm for powermeter
        warn_vbat = 4;
      #endif
      } else if (vbat>VBATLEVEL2_3S) warn_vbat = 1;
      else if (vbat>VBATLEVEL3_3S)   warn_vbat = 2;
      else                           warn_vbat = 4;
    #endif
 
    if ( rcOptions[BOXBEEPERON] )beeperOnBox = 1;
    else beeperOnBox = 0;
    
    #if defined(RCOPTIONSBEEP)
      static uint8_t i = 0,firstrun = 1, last_rcOptions[CHECKBOXITEMS];
                    
      if (last_rcOptions[i] != rcOptions[i])beep_toggle = 1;
        last_rcOptions[i] = rcOptions[i]; 
        i++;
      if(i >= CHECKBOXITEMS)i=0;
      
      if(firstrun == 1 && beep_confirmation == 0){
        beep_toggle = 0;    //only enable options beep AFTER gyro init
        beeperOnBox = 0;
      }        
      else firstrun = 0;
       
    #endif  
     
    #if defined(FAILSAFE)
      if ( failsafeCnt > (5*FAILSAVE_DELAY) && f.ARMED) {
        warn_failsafe = 1;                                                                   //set failsafe warning level to 1 while landing
        if (failsafeCnt > 5*(FAILSAVE_DELAY+FAILSAVE_OFF_DELAY)) warn_failsafe = 2;          //start "find me" signal after landing   
      }
      if ( failsafeCnt > (5*FAILSAVE_DELAY) && !f.ARMED) warn_failsafe = 2;                  // tx turned off while motors are off: start "find me" signal
      if ( failsafeCnt == 0) warn_failsafe = 0;                                              // turn off alarm if TX is okay
    #endif
    
    #if GPS
      if ((rcOptions[BOXGPSHOME] || rcOptions[BOXGPSHOLD]) && !f.GPS_FIX)warn_noGPSfix = 1;  
      else warn_noGPSfix = 0;
    #endif

    #if defined(ARMEDTIMEWARNING)
      if (armedTime >= ArmedTimeWarningMicroSeconds)warn_runtime = 1;
    #endif

    buzzerHandler();
    #if defined(PILOTLAMP)
      PilotLampHandler();
    #endif
  
    
  }

  void buzzerHandler(){ 
    /********************************************************************/
    /****                      Buzzer Handling                       ****/
    /********************************************************************/  
    // beepcode(length1,length2,length3,pause)
    //D: Double, L: Long, M: Middle, S: Short, N: None
    if (warn_failsafe == 2)      beep_code('L','N','N','D');                 //failsafe "find me" signal
    else if (warn_failsafe == 1) beep_code('S','L','L','S');                 //failsafe landing active              
    else if (beep_toggle == 1) {beep_code('S','N','N','N');      } 
    else if (beep_toggle == 2)    beep_code('S','S','N','N');       
    else if (beep_toggle > 2)     beep_code('S','S','S','N');         
    else if (warn_noGPSfix == 1) beep_code('S','S','N','S');    
    else if (beeperOnBox == 1)   beep_code('S','S','S','S');                 //beeperon
    else if (warn_runtime == 1 && f.ARMED == 1)beep_code('S','S','S','N'); //Runtime warning      
    else if (warn_vbat == 4)     beep_code('S','S','L','D');       
    else if (warn_vbat == 2)     beep_code('S','L','N','D');       
    else if (warn_vbat == 1)     beep_code('L','N','N','D'); 
    else if (beep_confirmation == 1) beep_code('L','N','N','L');    
    else if (beep_confirmation == 2) beep_code('L','L','N','L');   
    else if (beep_confirmation == 3) beep_code('L','L','L','L');
    else if (beep_confirmation == 4) beep_code('L','M','S','N');
    else if (beep_confirmation > 4) beep_code('L','L','L','L');
    else if (buzzerSequenceActive == 1) beep_code('N','N','N','N');                //if no signal is needed, finish sequence if not finished yet
    else{                                                                   //reset everything and keep quiet
      if (channelIsOn[1]) {
        channelIsOn[1] = 0;
        BUZZERPIN_OFF;
      }
    }  
  }
  void beep_code(char first, char second, char third, char pause){
    static char patternChar[4];
    uint16_t Duration;
    static uint8_t icnt = 0;
    
    if (buzzerSequenceActive == 0){    //only change sequenceparameters if prior sequence is done
      buzzerSequenceActive = 1;
      patternChar[0] = first; 
      patternChar[1] = second;
      patternChar[2] = third;
      patternChar[3] = pause;
    }
    switch(patternChar[icnt]) {
      case 'L': 
        Duration = 200; 
        break;
      case 'M': 
        Duration = 120; 
        break;
      case 'D': 
        Duration = 2000; 
        break;
      case 'N': 
        Duration = 0; 
        break;
      default:
        Duration = 50; 
        break;
    }
    if(icnt <3 && Duration!=0){
      useResource('S',Duration,50);
    }
    if (icnt >=3 && (channelLastToggleTime[1]<millis()-Duration) ){
      icnt=0;
      if (beep_toggle)beep_toggle = 0;
      if (beep_confirmation)beep_confirmation = 0;
      buzzerSequenceActive = 0;                              //sequence is now done, next sequence may begin
      if (channelIsOn[1]) {
        BUZZERPIN_OFF;
        channelIsOn[1] = 0;
      }
      return;
    }
    if (cycle_Done[1] == 1 || Duration == 0){
      if (icnt < 3){icnt++;} 
      cycle_Done[1] = 0;
      if (channelIsOn[1]) {
        BUZZERPIN_OFF;
        channelIsOn[1] = 0;
      }
    }  
  }  
  
    
#endif  //end of buzzer define


#if defined (PILOTLAMP) 
  //original code based on mr.rc-cam and jevermeister work
  //modified by doughboy to use timer interrupt

  #define PL_BUF_SIZE 8
  volatile uint8_t queue[PL_BUF_SIZE]; //circular queue
  volatile uint8_t head = 0;
  volatile uint8_t tail = 0;

/********************************************************************/
/****                   Pilot Lamp Handling                      ****/
/********************************************************************/
  void PilotLampHandler(){
    static int16_t  i2c_errors_count_old = 0;
    static uint8_t channel = 0;
    //==================I2C Error ===========================
    if (i2c_errors_count > i2c_errors_count_old+100){
      PilotLampSequence(100,B000111,2); //alternate all on, all off pattern
    }else if (beeperOnBox){
    //==================LED Sequence ===========================
      PilotLampSequence(100,B0101<<8|B00010001,4); //sequential pattern 
    }else{
    //==================GREEN LED===========================
      if (f.ARMED && f.ACC_MODE) useResource('G',1000,1000);
      else if (f.ARMED) useResource('G',1000,0);
      else useResource('G',0,0);    //switch off
    //==================BLUE LED===========================
      #if GPS
        if (!f.GPS_FIX) useResource('B',100,100);
        else if (rcOptions[BOXGPSHOME] || rcOptions[BOXGPSHOLD]) useResource('B',1000,1000);
        else useResource('B',100,1000);
      #else
        useResource('B',0,0);
      #endif   
    //==================RED LED===========================
      if (warn_failsafe==1)useResource('R',100,100);
      else if (warn_failsafe==2)useResource('R',1000,2000);
      else useResource('R',0,0);
   }
 }

//define your light pattern by bits, 0=off 1=on
//define up to 5 patterns that cycle using 15 bits, pattern starts at bit 0 in groups of 3
//  14 13 12 11 10 9 8 7 6 5 4 3 2 1 0            
//   R  B  G  R  B G R B G R B G R B G
//parameters speed is the ms between patterns
//           pattern is the 16bit (uses only 15 bits) specifying up to 5 patterns
//           num_patterns is the number of patterns defined
//example to define sequential light where G->B->R then back to G, you need 4 patterns
//           B00000101<<8 | B00010001
//example: to define alternate all on and al off, you only need two patterns
//           B00000111
  void PilotLampSequence(uint16_t speed, uint16_t pattern, uint8_t num_patterns){
    static uint32_t lastswitch = 0;
    static uint8_t seqno = 0;
    static uint16_t lastpattern = 0;  //since variables are static, when switching patterns, the correct pattern will start on the second sequence
    
    if(millis() < (lastswitch + speed))                                 
      return; //not time to change pattern yet
    lastswitch = millis();
   
    for (uint8_t i=0;i<3;i++) {
      uint8_t state = (pattern >>(seqno*3+i)) & 1; //get pattern bit
      //since value is multiple of 25, we can calculate time based on pattern position
      //i=0 is green, 1=blue, 2=red, green ON ticks is calculated as 50*(0+1)-1*25 = 25 ticks
      uint8_t tick = 50*(i+1);
      if (state)
        tick -=25;
      PilotLamp(tick);
      channelIsOn[i+2]=state;
    }
   seqno++;
   seqno%=num_patterns;
  }

  void PilotLamp(uint8_t count){
    if (((tail+1)%PL_BUF_SIZE)!=head) {
      queue[tail]=count;
      tail++;
      tail=(tail%PL_BUF_SIZE);
    }
  }
  //ISR code sensitive to changes, test thoroughly before flying
  ISR(PL_ISR) { //the interrupt service routine
   static uint8_t state = 0;
   uint8_t h = head;
   uint8_t t = tail;
   if (state==0) {
     if (h!=t) {
       uint8_t c = queue[h];
       PL_PIN_ON;
       PL_CHANNEL+=c;
       h = ((h+1) % PL_BUF_SIZE);
       head = h;
       state=1;
     }
   } else if (state==1) {
     PL_PIN_OFF;
     PL_CHANNEL+=PL_IDLE;
     state=0;
   } 
 }
#endif
/********************************************************************/
/****                         LED Handling                       ****/
/********************************************************************/
//Beware!! Is working with delays, do not use inflight!

void blinkLED(uint8_t num, uint8_t ontime,uint8_t repeat) {
  uint8_t i,r;
  for (r=0;r<repeat;r++) {
    for(i=0;i<num;i++) {
      #if defined(LED_FLASHER)
        switch_led_flasher(1);
      #endif
      #if defined(LANDING_LIGHTS_DDR)
        switch_landing_lights(1);
      #endif
      LEDPIN_TOGGLE; // switch LEDPIN state
      delay(ontime);
      #if defined(LED_FLASHER)
        switch_led_flasher(0);
      #endif
      #if defined(LANDING_LIGHTS_DDR)
        switch_landing_lights(0);
      #endif
    }
    delay(60); //wait 60 ms
  }
}

/********************************************************************/
/****                         Global Handling                    ****/
/********************************************************************/

  int useResource(char resource, uint16_t pulse, uint16_t pause){ 
    static uint8_t channel = 0; 
    channel = ResourceToChannel(resource);
    if (!channelIsOn[channel] && (millis() >= (channelLastToggleTime[channel] + pause))&& pulse != 0) {	         
      channelIsOn[channel] = 1;      
      ChannelToOutput(channel,1);
      channelLastToggleTime[channel]=millis();      
    } else if (channelIsOn[channel] && (millis() >= channelLastToggleTime[channel] + pulse)|| (pulse==0 && channelIsOn[channel]) ) {       
      channelIsOn[channel] = 0;
      ChannelToOutput(channel,0);
      channelLastToggleTime[channel]=millis();
      cycle_Done[channel] = 1;     
    } 
  } 
  
  int ResourceToChannel(uint8_t resource){
    uint8_t channel =0;
    switch(resource) {
      case 'L': 
        channel = 0;
        break;
      case 'S': 
        channel = 1;
        break;
      case 'G': 
        channel = 2;
        break;
      case 'B': 
        channel = 3;
        break;
      case 'R': 
        channel = 4;
        break;
      default:
        channel = 0;
        break;
    }
    return channel;
  }
  
  void ChannelToOutput(uint8_t channel, uint8_t activate){
     switch(channel) {        
          case 1:
            if (activate == 1) {BUZZERPIN_ON;}
            else {BUZZERPIN_OFF;}
            break; 
        #if defined (PILOTLAMP) 
          case 2:
            if (activate == 1) PilotLamp(PL_GRN_ON);
            else PilotLamp(PL_GRN_OFF);
            break;
          case 3: 
            if (activate == 1)PilotLamp(PL_BLU_ON);
            else PilotLamp(PL_BLU_OFF);
            break;
          case 4: 
            if (activate == 1)PilotLamp(PL_RED_ON);
            else PilotLamp(PL_RED_OFF);
            break;
        #endif
	case 0:	
        default:
          if (activate == 1){LEDPIN_ON;}
          else {LEDPIN_OFF;}
          break;
      }
      return;
  }


/********************************************************************/
/****                      LED Ring Handling                     ****/
/********************************************************************/

#if defined(LED_RING)
  #define LED_RING_ADDRESS 0x6D //7 bits
  
  void i2CLedRingState() {
    uint8_t b[10];
    static uint8_t state;
    
    if (state == 0) {
      b[0]='z'; 
      b[1]= (180-heading)/2; // 1 unit = 2 degrees;
      i2c_rep_start(LED_RING_ADDRESS<<1);
      for(uint8_t i=0;i<2;i++)
        i2c_write(b[i]);
      i2c_stop();
      state = 1;
    } else if (state == 1) {
      b[0]='y'; 
      b[1]= constrain(angle[ROLL]/10+90,0,180);
      b[2]= constrain(angle[PITCH]/10+90,0,180);
      i2c_rep_start(LED_RING_ADDRESS<<1);
      for(uint8_t i=0;i<3;i++)
        i2c_write(b[i]);
      i2c_stop();
      state = 2;
    } else if (state == 2) {
      b[0]='d'; // all unicolor GREEN 
      b[1]= 1;
      if (f.ARMED) b[2]= 1; else b[2]= 0;
      i2c_rep_start(LED_RING_ADDRESS<<1);
      for(uint8_t i=0;i<3;i++)
        i2c_write(b[i]);
      i2c_stop();
      state = 0;
    }
  }
  
  void blinkLedRing() {
    uint8_t b[3];
    b[0]='k';
    b[1]= 10;
    b[2]= 10;
    i2c_rep_start(LED_RING_ADDRESS<<1);
    for(uint8_t i=0;i<3;i++)
      i2c_write(b[i]);
    i2c_stop();
  
  }
#endif


/********************************************************************/
/****                    LED flasher Handling                    ****/
/********************************************************************/

#if defined(LED_FLASHER)
  static uint8_t led_flasher_sequence = 0;
  /* if we load a specific sequence and do not want it change, set this flag */
  static enum {
  	LED_FLASHER_AUTO,
  	LED_FLASHER_CUSTOM
  } led_flasher_control = LED_FLASHER_AUTO;
  
  void init_led_flasher() {
    LED_FLASHER_DDR |= (1<<LED_FLASHER_BIT);
    LED_FLASHER_PORT &= ~(1<<LED_FLASHER_BIT);
  }
  
  void led_flasher_set_sequence(uint8_t s) {
    led_flasher_sequence = s;
  }
  
  void inline switch_led_flasher(uint8_t on) {
    if (on) {
      LED_FLASHER_PORT |= (1<<LED_FLASHER_BIT);
    } else {
      LED_FLASHER_PORT &= ~(1<<LED_FLASHER_BIT);
    }
  }
  
  void auto_switch_led_flasher() {
    uint8_t seg = (currentTime/1000/125)%8;
    if (led_flasher_sequence & 1<<seg) {
      switch_led_flasher(1);
    } else {
      switch_led_flasher(0);
    }
  }
  
  /* auto-select a fitting sequence according to the
   * copter situation
   */
  void led_flasher_autoselect_sequence() {
    if (led_flasher_control != LED_FLASHER_AUTO) return;
    #if defined(LED_FLASHER_SEQUENCE_MAX)
    /* do we want the complete illumination no questions asked? */
    if (rcOptions[BOXLEDMAX]) {
    #else
    if (0) {
    #endif
      led_flasher_set_sequence(LED_FLASHER_SEQUENCE_MAX);
    } else {
      /* do we have a special sequence for armed copters? */
      #if defined(LED_FLASHER_SEQUENCE_ARMED)
      led_flasher_set_sequence(f.ARMED ? LED_FLASHER_SEQUENCE_ARMED : LED_FLASHER_SEQUENCE);
      #else
      /* Let's load the plain old boring sequence */
      led_flasher_set_sequence(LED_FLASHER_SEQUENCE);
      #endif
    }
  }
  
  #endif
  
  #if defined(LANDING_LIGHTS_DDR)
  void init_landing_lights(void) {
    LANDING_LIGHTS_DDR |= 1<<LANDING_LIGHTS_BIT;
  }
  
  void inline switch_landing_lights(uint8_t on) {
    if (on) {
      LANDING_LIGHTS_PORT |= 1<<LANDING_LIGHTS_BIT;
    } else {
      LANDING_LIGHTS_PORT &= ~(1<<LANDING_LIGHTS_BIT);
    }
  }
  
  void auto_switch_landing_lights(void) {
    if (rcOptions[BOXLLIGHTS]
    #if defined(LANDING_LIGHTS_AUTO_ALTITUDE) & SONAR
  	|| (sonarAlt >= 0 && sonarAlt <= LANDING_LIGHTS_AUTO_ALTITUDE && f.ARMED)
    #endif
    ) {
      switch_landing_lights(1);
    } else {
      switch_landing_lights(0);
    }
  }
#endif
