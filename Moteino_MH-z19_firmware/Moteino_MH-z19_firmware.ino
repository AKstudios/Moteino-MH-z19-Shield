// ************************************************************

// Moteino MH-z19 shield firmware
// Developed by AKstudios
// Updated on 07/04/2020

// Libraries ************************************************************

#include <RFM69.h>  //  https://github.com/LowPowerLab/RFM69
#include <SPI.h>
#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <SoftwareSerial.h>

// Global variables & objects ************************************************************

// define node parameters
char node[] = "77";
#define NODEID        77 // same sa above - must be unique for each node on same network (range up to 254, 255 is used for broadcast)
#define GATEWAYID     1
#define NETWORKID     101
#define FREQUENCY     RF69_915MHZ //Match this with the version of your Moteino! (others: RF69_433MHZ, RF69_868MHZ)
#define ENCRYPTKEY    "Tt-Mh=SQ#dn#JY3_" //has to be same 16 characters/bytes on all nodes, not more not less!
#define IS_RFM69HW    //uncomment only for RFM69HW! Leave out if you have RFM69W!
#define LED           9 // led pin

// define radio parameters
RFM69 radio;
char dataPacket[150];

// define MH-z19 global variables
SoftwareSerial mySerial(4, 3); // RX, TX
byte cmd[9] = {0xFF,0x01,0x86,0x00,0x00,0x00,0x00,0x00,0x79};
unsigned char response[18]; // expect a 9 bytes response, give twice the size just in case things go crazy, it reduces likelyhood of crash/buffer overrun
unsigned long th, tl, h, l, ppm1, ppm2, ppm3 = 0;
#define pwmPin 6
int total_ppm;
int prevVal = LOW;
int ppm, _ppm;
float co2;


// Interrupt service routine for WatchDog Timer ************************************************************
ISR(WDT_vect)  
{
  wdt_disable();  // disable watchdog
}

// Setup function ************************************************************
void setup()
{
  pinMode(10, OUTPUT); // Radio SS pin set as output
  pinMode(9, OUTPUT);  // pin 9 controls LED
  
  Serial.begin(115200);
  Serial.println("Bootup");
  delay(5);
  
  mySerial.begin(9600);   // initialize MHz-19 in UART mode
  //pinMode(pwmPin, INPUT);   // initialize MHz-19 in PWM mode
  
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
#ifdef IS_RFM69HW
  radio.setHighPower(); //uncomment only for RFM69HW!
#endif
  radio.encrypt(ENCRYPTKEY);

  getCO2();  // get first reading
  
  fadeLED(LED); // fade LED to indicate device booted up and is ready
}

// Sleep function ************************************************************
void sleep()
{
  Serial.flush(); // empty the send buffer, before continue with; going to sleep
  
  radio.sleep();
  
  cli();          // stop interrupts
  MCUSR = 0;
  WDTCSR  = (1<<WDCE | 1<<WDE);     // watchdog change enable
  WDTCSR  = 1<<WDIE | (1<<WDP3) | (0<<WDP2) | (0<<WDP1) | (1<<WDP0); // set  prescaler to 8 second
  sei();  // enable global interrupts

  byte _ADCSRA = ADCSRA;  // save ADC state
  ADCSRA &= ~(1 << ADEN);

  asm("wdr");
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();       

  sleep_enable();  
  sleep_bod_disable();
  sei();       
  sleep_cpu();   
    
  sleep_disable();   
  sei();  

  ADCSRA = _ADCSRA; // restore ADC state (enable ADC)
  delay(1);

  Serial.flush(); // empty the send buffer, before continue with; going to sleep
}


// loop function ************************************************************
void loop() 
{
  sleep();
  co2 = getCO2();
  createDataPacket(co2);
  
  Serial.println(dataPacket);
  delay(5);

  // send datapacket
  radio.sendWithRetry(GATEWAYID, dataPacket, strlen(dataPacket), 5, 100);  // send data, retry 5 times with delay of 100ms between each retry
  dataPacket[0] = (char)0; // clearing first byte of char array clears the array
 
  blinkLED(LED, 5);
}


// get CO2 levels ************************************************************
float getCO2()
{
  // MH-z19 UART
  while(mySerial.available() > 0)  // clear out buffer
    char x = mySerial.read();
    
  mySerial.write(cmd,9);
  delay(1);
  
  if(mySerial.available() > 0 && (unsigned char)mySerial.peek() != 0xFF)
    mySerial.read();
    
  mySerial.readBytes(response, 9);
  
  unsigned int responseHigh = (unsigned int) response[2];
  unsigned int responseLow = (unsigned int) response[3];
  ppm = (256 * responseHigh) + responseLow;
  
  co2 = (0.2*ppm) + (0.8*_ppm);  // real-time exponential smoothing of data with a damping factor of 0.8
  
  _ppm = (int)co2;  // save old reading;

  /*
  // MH-z19 PWM
  do
  {
    th = pulseIn(pwmPin, HIGH, 1004000) / 1000;
    tl = 1004 - th;
    //ppm2 = 2000 * (th-2)/(th+tl-4);
    ppm1 = 5000 * (th-2)/(th+tl-4);
  }while(th == 0);
  */
  /*
  while(1)
  {
    long tt = millis();
    int myVal = digitalRead(pwmPin);
    
    // If there's a change in pin state
    if(myVal == HIGH)
    {
      if (myVal != prevVal)
      {
        h = tt;
        tl = h - l;
        prevVal = myVal;
      }
    }
    
    else
    {
      if (myVal != prevVal)
      {
        l = tt;
        th = l - h;
        prevVal = myVal;
        ppm1 = 5000 * (th - 2) / (th + tl - 4);
        break;
      }  
    }
  }
  */

  return co2;
}

// create datapacket ************************************************************
void createDataPacket(float co2)
{
  // define character arrays for all variables
  char _c[7];
  
  // convert all flaoting point and integer variables into character arrays
  dtostrf(co2, 1, 0, _c); 
  delay(1);
  
  dataPacket[0] = 0;  // first value of dataPacket should be a 0
  
  // create datapacket by combining all character arrays into a large character array
  strcat(dataPacket, "i:");
  strcat(dataPacket, node);
  strcat(dataPacket, ",c:");
  strcat(dataPacket, _c);
  delay(1);
}


// Fade LED ************************************************************
void fadeLED(int pin)
{
  int brightness = 0;
  int fadeAmount = 5;
  for(int i=0; i<510; i=i+5)  // 255 is max analog value, 255 * 2 = 510
  {
    analogWrite(pin, brightness);  // pin 9 is LED
  
    // change the brightness for next time through the loop:
    brightness = brightness + fadeAmount;  // increment brightness level by 5 each time (0 is lowest, 255 is highest)
  
    // reverse the direction of the fading at the ends of the fade:
    if (brightness <= 0 || brightness >= 255)
    {
      fadeAmount = -fadeAmount;
    }
    // wait for 20-30 milliseconds to see the dimming effect
    delay(10);
  }
  digitalWrite(pin, LOW); // switch LED off at the end of fade
}


// blink LED ************************************************************
void blinkLED(int pin, int ms)
{
  digitalWrite(pin, HIGH); // switch LED off at the end of fade
  delay(ms);
  digitalWrite(pin, LOW); // switch LED off at the end of fade
}

// **********************************************************************
