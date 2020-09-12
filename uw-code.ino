/*
   Monitor sump, hvac, and water heater.
   Report to ThingSpeak.com via webhooks
    C. Catlett May 2017

                                          */

#include <OneWire.h>
#include <DS18B20.h>
#include "DS18.h"
#include "math.h"

// sensor pins
#define waterPin    D0                      // pin for ds18b20-A to water heater chimney
#define hvacPin     A0                      // pin for HVAC fan current sensor
#define sumpPin     A1                      // pin for sump pump current sensor


// ds18b20 temperature sensor instance
DS18 sensor(D0);

// global constant parameters
int     Window          = 1800000;          // 30 min window for sump runcount
int     motorON         = 100;              // need to calibrate - resting state ~40-50
int     sumpCheckFreq   = 2002;             // check sump every 2 seconds
                                // prime #s so these two interrupts very rarely fire together, which
                                // would probably make the photon faint so why risk it
int     allCheckFreq    = 17321;            // check everyone else every once-in-a-while ms
int     reportFreq      = 23431;            // report a var to ThingSpeak every less-often ms
int     reportCount     = 0;
#define HIST              32               // number of sump checks to keep in short term memory

// global variables
bool    sumpOn          = false;            // state variables
bool    hvacOn          = false;
bool    heaterOn        = false;
int     sumpCur         = 0;                // sensor values
int     hvacCur         = 0;
double  waterTemp       = 70;
double  lastTemp        = 70;
char    reportStrg       [64];

// statistics variables
int     sumpHistory      [HIST];            // all sump readings between reports
int     sumpPointer     = 0;
bool    sumpEvent       = false;
bool    hvacEvent       = false;
int     sumpStart       = 0;                // start time of sump event
int     sumpDuration    = 0;                // duration of sump event
int     hvacStart       = 0;                // start time of hvac event
int     hvacDuration    = 0;                // duration of hvac event

// sump duty cycle variables
#define SMAX              16                // maximum we might ever see the sump run in a window
int     sumpRuns        [SMAX];             // keep track of how many time sump runs in a given window of time
int     dutyWindow      = 1800000;          // set the duty cycle window to 30 minutes
int     dutyPtr         = 0;                // pointer into dutyWindow array
int     runCount        = 0;                // sump runcount past Window ms


// interrupts and timers
Timer sumpTimer(sumpCheckFreq, checkSump);  // every checkFreq ms check the sump current
Timer allTimer(allCheckFreq,    checkAll);  // every allCheckFreq ms check the others
Timer alertTimer(Window, siren);            // for ifttt lets blow the wistle every 30min if needed

int     lastReport      = 0;

void setup() {
    Time.zone             (-5);
    Particle.syncTime       ();
    pinMode(sumpPin,    INPUT);
    pinMode(hvacPin,    INPUT);
    pinMode(waterPin,   INPUT);
    Serial.begin      (115200);
    // set up vars to export (tip- compiler barfs if the variable alias (in quotes) is longer than 12 characters)
    Particle.variable("sumpCount",  runCount);
    Particle.variable("sumpCur",    sumpCur);
    Particle.variable("hvacCur",    hvacCur);
    Particle.variable("heatTemp",   waterTemp);
    // zero out the sump history
    for (int i=0; i<HIST; i++) sumpHistory[i] = 0;
    // set timers
    sumpTimer.start();
    allTimer.start();
    alertTimer.start();
}

void loop() {

    if ((millis() - lastReport) > reportFreq) {         // time to report yet? 
       int cases = 5;                                  // hard code #cases here feels like a kluge
        switch ((reportCount++) % cases) {              // simple round robin reporting
            case 0:                                     // sump current
                for (int j=0;j<HIST;j++) {
                    sumpCur = max (sumpCur, sumpHistory[j]);
                    sumpHistory[j]=0;
                }
                Particle.publish("sumpCurrent", String(sumpCur),    PRIVATE);
                break;
            case 1:                                     // hvac current
                Particle.publish("hvacCurrent", String(hvacCur),    PRIVATE);
                break;
            case 2:                                     // water heater chimney temperature
                Particle.publish("waterTemp",   String(waterTemp),  PRIVATE);
                break;
            case 3:                                     // duration of last hvac event (if there was one; 0 if not)
                if (hvacEvent) {
                    if (!hvacOn) Particle.publish("hvacEvent", String(hvacDuration), PRIVATE);
                    hvacEvent = false;
                } else {
                    Particle.publish("hvacEvent", "0", PRIVATE);
                }
                break;
            case 4:                                     // duration of last sump event (if there was one; 0 if not)
                if (sumpEvent) {
                    Particle.publish("sumpEvent", String(sumpDuration), PRIVATE);
                    sumpEvent = false;
                } else {
                    Particle.publish("sumpEvent", "0", PRIVATE);
                }
                break;
            }
            lastReport = millis();
    }
}
/************************************/
/***        TIMER FUNCTIONS       ***/
/************************************/
//
// timed check of sump
//
void checkSump () {

    //Particle.publish("timerDebug", "checked sump", PRIVATE);
    sumpCur = analogRead(sumpPin);
    sumpPointer = (sumpPointer+1) % HIST;
    sumpHistory[sumpPointer] = sumpCur;
    if (sumpCur > motorON) {
        if (!sumpOn) {
            //sumpEvent = true;
            sumpStart = millis();
        }
        sumpOn = true;
    } else {
        if (sumpOn) {
            sumpEvent = true;
            sumpDuration = (millis() - sumpStart)/1000;     // sump event duration in seconds
            sumpRuns[dutyPtr] = millis();                   // record the event in the duty cycle counter buffer
            dutyPtr = (dutyPtr + 1) % SMAX;                 // advance pointer in the circular cycle counter buffer
            runCount = 0;                                   // how many of the past SMAX runs are less than dutyWindow ms prior to now?
            for (int i=0; i<SMAX; i++) if ((millis() - sumpRuns[i]) < dutyWindow) runCount++;
        }
        sumpOn = false;
    }
}
//
// timed check of everything but sump
//
void checkAll () {
    int reading;
    
    // hvac
    hvacCur = analogRead(hvacPin);
    if (hvacCur > motorON) {
        if (!hvacOn) {
            hvacEvent = true;
            hvacStart = millis();
        }
        hvacOn = true;
    } else {
        if (hvacOn) {
            hvacEvent = true;
            hvacDuration = (millis() - hvacStart)/60000;     // hvac event duration in minutes
        }
        hvacOn = false;
    }
    // water heater
    lastTemp  = waterTemp;
    waterTemp = getTemp();
    if (waterTemp < 50) waterTemp = lastTemp;
}

void siren(){
    if (runCount > 6) Particle.publish("Danger", "sump", PRIVATE) ;
}
/************************************/
/***      ON-DEMAND FUNCTIONS     ***/
/************************************/

//
// poll the temperature sensor
//

double getTemp() {
  // Read the next available 1-Wire temperature sensor
  if (sensor.read()) {
    // Do something cool with the temperature
    Serial.printf("Temperature %.2f C %.2f F ", sensor.celsius(), sensor.fahrenheit());
    //Particle.publish("temperature", String(sensor.celsius()), PRIVATE);

    // Additional info useful while debugging
    printDebugInfo();

  // If sensor.read() didn't return true you can try again later
  // This next block helps debug what's wrong.
  // It's not needed for the sensor to work properly
  } else {
    // Once all sensors have been read you'll get searchDone() == true
    // Next time read() is called the first sensor is read again
    if (sensor.searchDone()) {
      Serial.println("No more addresses.");
      // Avoid excessive printing when no sensors are connected
      delay(250);
    //Particle.publish("tempError", "sensor not found", PRIVATE);
    // Something went wrong
    } else {
      printDebugInfo();
    }
  }
  Serial.println();
  return (sensor.fahrenheit());
}

void printDebugInfo() {
  // If there's an electrical error on the 1-Wire bus you'll get a CRC error
  // Just ignore the temperature measurement and try again
  if (sensor.crcError()) {
    Serial.print("CRC Error ");
  }

  // Print the sensor type
  const char *type;
  switch(sensor.type()) {
    case WIRE_DS1820: type = "DS1820"; break;
    case WIRE_DS18B20: type = "DS18B20"; break;
    case WIRE_DS1822: type = "DS1822"; break;
    case WIRE_DS2438: type = "DS2438"; break;
    default: type = "UNKNOWN"; break;
  }
  Serial.print(type);

  // Print the ROM (sensor type and unique ID)
  uint8_t addr[8];
  sensor.addr(addr);
  Serial.printf(
    " ROM=%02X%02X%02X%02X%02X%02X%02X%02X",
    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]
  );

  // Print the raw sensor data
  uint8_t data[9];
  sensor.data(data);
  Serial.printf(
    " data=%02X%02X%02X%02X%02X%02X%02X%02X%02X",
    data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8]
  );
}

