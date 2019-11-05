#include <StaticThreadController.h>
#include <Thread.h>
#include <ThreadController.h>
#include "Adafruit_FONA.h"
#include <SoftwareSerial.h>
//#include <Adafruit_GPS.h>
#define USE_ARDUINO_INTERRUPTS true
#include<PulseSensorPlayground.h>
#define PIN_BUTTON 2 /*Button pin*/
#define FONA_RX 10 /*GSM module recive data*/
#define FONA_TX 9 /*GSM module transmited data*/
#define FONA_RST 6 /* this is reset pin for the module*/
//#define GPSECHO false
#define LED7 7//LED to heart BPM
#define PulseSensorPin 11// Pin read heart bpm
//#define GPSSerial Serial1
#define FONA_RI_INTERRUPT 0

#include<Adafruit_NeoPixel.h>
#define PIN8 8
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1,PIN8,NEO_RGB + NEO_KHZ800);
//Connect to the GPS on the hardware port
//Adafruit_GPS GPS(&GPSSerial);

/*Adjustment to GSM module*/
SoftwareSerial fonaGST = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaGST;
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

Thread dataThread = Thread();//Thread send data to server
Thread bluetoothThread = Thread();
/*object pulse sensor*/
PulseSensorPlayground pulseSensor;

int vbat;//for check charge battery
bool callFromEmergency = false;// if have call from emergency
bool callToEmergency = false; // if button pressed
int time;//timer for button
uint8_t previousSeconds = 0;
bool flagForTime = true;//When button is press initialization variable previousSeconds
bool end_calling = true;// if call end/not end
const char *url = "http://dbrainz-flora-server-app.herokuapp.com/postFloraData";
int pulse, gsm = 9;
bool gps = false, bluetooth = true;
int satellites;
int Signal;
const char *EmergencyPhone = "0546774630";
const int8_t GSTSerial = 1;
bool gpsInitialization = false;
char conn_BT_HFG[5];
int8_t counter = 0, passedTime, currentSecond;
char Bchar;
int8_t index = 0;
String DataFromGPS[15];
char *dataGPS[15];
int8_t previousTimeForBluetooth,currentTimeForBluetooth;
bool startTime = true;
bool status_sos = false;
int8_t counterForSendStatusSos = 1;

void setup() {
  Serial.begin(9600);
  time = millis();//start timer
  turn_on_GSM_module_and_GPRS();//turn on GSM and GPRS
  turn_on_GPS();//turn on GPS
  strip.begin();//turn on led light
  turnOffLED8();
  turn_on_Bluetooth();//turn on bluetooth
  init_button();//initialize button
  dataThread.onRun(sendDataToServer);//initialize timer to function for send data on server
  dataThread.setInterval(2000);//set interval time
  bluetoothThread.onRun(BluetoothTimer);//initialize timer for function bluetooth
  bluetoothThread.setInterval(60000);//set interval for bluetooth timer
  initialization_Pulse_sensor();//initialize bluetooth
}

void loop() {
    //heart rate measurement
    getHeartPulse();
    pulseSensor.pause();//Turn on interrupt for pulse sensor

    if(bluetoothThread.shouldRun()){
      bluetoothThread.run();
    }
    
    //Check if pressed button
    if (callFromEmergency == false && end_calling == true) {
      check_if_click_button_help(); 
    }

    //Check if emergency call
    if (callToEmergency == false && end_calling == true) {
      incomingCall();
    }

    //If call is end turn hang up
    if ((callToEmergency == true || callFromEmergency == true) && digitalRead(PIN_BUTTON) == LOW) {
         hang_up();
    }

    //Send all data to server
    if (dataThread.shouldRun()) {
        getGeoLocation();//Get geo data
        getPrecentageBattery(); //Get battery precentage
        checkBluetoothWork(); //Check if bluetooth work
        dataThread.run();//send all data to server
        
        if(counterForSendStatusSos == 4 && status_sos == true){
           status_sos = false;
           counterForSendStatusSos = 1;
        }
        if(status_sos == true){
          counterForSendStatusSos++;
        }
    }
    //Check communication status
    getCallStatus(); 
    pulseSensor.resume();//Turn off interrupt for pulse sensor
    delay(3000);
}

/*********************************/
/*Init Mac address and turn on BT*/
/*********************************/
void turn_on_Bluetooth() {
 turnOnBlue8();
  fona.println(F("AT+BTPOWER=0"));
   delay(1000);
   flushFonaSerial();
  fona.println(F("AT+BTPOWER=1"));
   delay(1000);
   flushFonaSerial();
  fona.println(F("AT+BTVIS=1"));
   delay(1000);
  flushFonaSerial();
  fona.println(F("AT+BTUNPAIR=0"));
   delay(1000);
  flushFonaSerial();
  fona.println(F("AT+BTSCAN=1"));
  delay(15000);
  flushFonaSerial();
  connectBluetooth();
  turnOffLED8();
}

/***********************************/
/*Pairing bluetooth with gsm module*/
/***********************************/
void connectBluetooth() {
  fona.println(F("AT+BTPAIR=0,1")); //Bluetooh pairing to headset
  delay(10000);
  flushFonaSerial();
  fona.println(F("AT+BTCONNECT=1,6")); //Bluetooth connected with headset
  delay(3000);
  flushFonaSerial();
  fona.println(F("AT+BTVIS=0"));//when bluetooth is connected to the headset the bluetooth will become invisible
  delay(1000);
  flushFonaSerial();
}

/***************************************************************************/
/*Check if bluetooth work if don't work, programm call recnnection function*/
/***************************************************************************/
void checkBluetoothWork() {
  index = counter = 0;
  const char arr2[] = "C: 1";
  fona.println(F("AT+BTSTATUS?"));
  delay(50);
  while (fona.available()) {
    Bchar = fona.read();
    if (Bchar == '\n') {
      counter++;
    }
    if (counter == 3 || counter == 4) {
      if (index < 4 && Bchar != '\n') {
        if (arr2[index] == Bchar) {
          conn_BT_HFG[index++] = Bchar;
          if (index == 4)
            conn_BT_HFG[index] = '\0';
          if (String(conn_BT_HFG) == String(arr2)) {
            bluetooth = true;
            return;
          }
        }
      }
    }
  }
}

/*************/
/*Turn on GPS*/
/*************/
void turn_on_GPS() {
    fona.enableGPS(true);
}

/*****************************/
/*Turn on GSM module and GPRS*/
/*****************************/
void turn_on_GSM_module_and_GPRS() {
  fonaSerial->begin(9600);
  if (!fona.begin(*fonaSerial)) {
    while (1);
  }
  fona.setGPRSNetworkSettings(F("we"), F(""), F("")); //Registered APN
  fona.enableGPRS(false);//If GPRS turn on we turn off
  while (true) {
    if (!fona.enableGPRS(true)) { // turn on GPRS and Network turn on
    }
    else break;
    delay(1000);
  }
  //Enable incoming call notification.
  fona.callerIdNotification(true, FONA_RI_INTERRUPT);

  //Automatically answering the Call after 3 rings
  fona.println(F("ATS0=3"));
}

/******************/
/*Pull up resistor*/
/******************/
void init_button() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);//pull up resistor in micro controller
  digitalWrite(PIN_BUTTON, HIGH);
}

/******************/
/*Pull up resistor*/
/******************/
void initialization_Pulse_sensor() {
  pulseSensor.analogInput(PulseSensorPin);
  pulseSensor.blinkOnPulse(LED7);
  pulseSensor.setThreshold(550);
  pulseSensor.begin();
}
void flushSerial() {
  while (Serial.available())
    Serial.read();
}

/**************/
/*Clean buffer*/
/**************/
void flushFonaSerial() {
  while (fona.available()) {
    fona.read();
  }
}

/*****************/
/*Get heart pulse*/
/*****************/
void getHeartPulse() {
  pulse = pulseSensor.getBeatsPerMinute();
  pulseSensor.sawStartOfBeat();
  Serial.println(pulse);
  delay(20);
}

/**************************/
/*Get current geo location*/
/**************************/
void getGeoLocation() {
  char gpsdata[120];
  index = 0;
 if(fona.GPSstatus() == 2 || fona.GPSstatus() == 3){
  gpsInitialization = true;
  fona.getGPS(0,gpsdata,120);
  char *buff = strtok(gpsdata," ,");
  while(buff!=NULL){
    *(dataGPS + index++) = buff;
    buff = strtok(NULL,",");
  }
 }
}

/*********************************/
/*Turn on if bluetooth don't work*/
/*********************************/
void BluetoothTimer(){
   if (bluetooth == false) {
       turn_on_Bluetooth();
      }
        flushFonaSerial();
        bluetooth = false;
}
/*******************************/
/*Function communication status*/
/*******************************/
void getCallStatus() {
  gsm = fona.getCallStatus();
}

/*****************************/
/*Check if click s.o.s button*/
/*****************************/
void check_if_click_button_help() {
  flagForTime = true;
  while (digitalRead(PIN_BUTTON) == LOW) {
    callToEmergency = true;
    if (flagForTime == true) {
      flagForTime = false;
      previousSeconds = getHowMuchPassedTime();
    }
    time = getHowMuchPassedTime();
    if (time >= previousSeconds + 3) {
      status_sos = true; 
      makeCall();
    }
  }
}

/********************************/
/*Turn off LED if client hang up*/
/********************************/
void hang_up() {
  fona.hangUp();
  callToEmergency = false;
  callFromEmergency = false;
  end_calling = true;
  turnOffLED8();
}

/************************/
/*Make call to emergency*/
/************************/
void makeCall() {
  fona.callPhone(EmergencyPhone);
  end_calling = false;
  turnOnRed8();
}

/**********************/
/*When someone is call*/
/**********************/
void incomingCall() {
  char phone[32] = {0};
  getCallStatus();
  if (gsm == 3 || gsm == 4) {
    if (fona.incomingCallNumber(phone)) {
      if (String(phone) == String(EmergencyPhone)) {
        callFromEmergency = true;
        end_calling = false;
        fona.pickUp(); //to lift tube
        turnOnRed8();
      }
    }
  }
}

///*************************/
///*Display how much charge*/
///*************************/
void getPrecentageBattery() {
  fona.getBattPercent(&vbat);
}

/*********************************/
/*If button press check the timer*/
/*********************************/
byte getHowMuchPassedTime() {
  time = millis() / 1000;
  return time % 60;
}

/*************************************/
/*Send stream data to server nodechef*/
/*************************************/
void sendDataToServer() {
  uint16_t statuscode;
  int16_t length;
  char data[250];
  if (gsm == 0 || gsm == 3 || gsm == 4 || gsm == 1) {
    gsm = 1;
  }
  else {
    gsm = 0;
  }
 
  //flushSerial();
  flushFonaSerial();
  sprintf(data, "{\"GSTSerial\":\"%d\",\"latitude\":\"%s\",\"longitude\":\"%s\",\"satellites\":\"%d\",\"pulse\":\"%d\",\"battery\":\"%d\",\"gps_status\":\"%d\",\"bt_status\":\"%d\",\"gsm_status\":\"%d\",\"sos_status\":\"%d\"}", GSTSerial,dataGPS[3] ,dataGPS[4] , atoi(dataGPS[13]), pulse, vbat,fona.GPSstatus(), bluetooth, gsm,status_sos);
  if(!fona.HTTP_POST_start(url, F("application/json"), (uint8_t*)data, strlen(data), &statuscode, (uint16_t*)&length)){
    Serial.println("Failde");
  }else
  Serial.println(data);
  fona.HTTP_POST_end();
  flushFonaSerial();
}

/***************/
/*Led for Calls*/
/***************/
void turnOnRed8(){
  strip.setBrightness(50);
  strip.show();
  colorWipe(strip.Color(255,0,0));//Green
}
/*******************/
/*Led for Bluetooth*/
/*******************/
void turnOnBlue8(){
  strip.setBrightness(50);
  strip.show();
  colorWipe(strip.Color(0,0,255));//Blue
}
void colorWipe(uint32_t c){
  strip.setPixelColor(0,c);
  strip.show();
}
void turnOffLED8(){
  strip.setBrightness(0);
  strip.show();
}
