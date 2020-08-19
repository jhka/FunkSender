/*
 * FunkSender
 * 
 * V1.0, 19.08.2020
 * 
 * Kondensatoren:
 *                           x-----(opt. 220 uF -------x
 *                           x---------  10 uF --------x
 *                           x--------- 100 nF --------x
 *                           |          _____          | 
 *  nRF24L01  VCC, pin2 --- VCC       1|o A  |14      GND --- nRF24L01 GND, pin1
 *  DS18B20 Mittlerer Pin-- PB0 (10)  2|  T  |13      AREF
 *                          PB1 ( 9)  3|  t  |12 ( 1) PA1
 *                          PB3 (  )  4|  i  |11 ( 2) PA2 --- LED (220 Ohm)
 *                          PB2 ( 8)  5|  n  |10 ( 3) PA3 --- nRF24L01 CSN, pin4 
 *  nRF24L01 CE, pin3       PA7 ( 7)  6|  y  |9  ( 4) PA4 --- nRF24L01 SCK, pin5
 *  nRF24L01 MISO, pin7 --- PA6 ( 6)  7|_____|8  ( 5) PA5 --- nRF24L01 MOSI, pin6
 *  
 */


// Sleep-Funktionen
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

// Kommunikation mit Temperatursensor DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>

// Achtung - neueste RF24 Library von TMRh20 verwenden
#include "RF24.h"

// Eindeutige ID des Senders definieren
#define SensorID 1
#define MeasurementID 1 // ID für verschiedene Messungen (inkl. Erweiterungen): 1: Temperatur, 2: Luftfeuchtigkeit, 3: Bodenfeuchtigkeit, 4: Öffnungsschalter, 5: Spannung, ...

// Definierte PINS
#define LEDPIN        2
#define ONE_WIRE_BUS  10 
#define CE_PIN        3
#define CSN_PIN       7

// Temperatur-Parameter
OneWire oneWire(ONE_WIRE_BUS);  // OneWire Bus
DallasTemperature sensors(&oneWire); // Sensor auf OneWire-Bus
DeviceAddress tempDeviceAddress;  // Adresse desSensors
    // Auflösung, max. Zeit für Temp-Conversion
    // 9 bit   10 bit   11 bit    12 bit
    // 0.5°C,  0.25°C,  0.125°C,  0.0625°C
    // 93,75    187,5    375        750 ms
int res = 11;                         // Auflösung des Sensors
float temperature = 0.0;              // Speichert Temperatur
boolean getTemp = false;              // Flag um zu zeigen, das Temp.Abfrage gestartet wurde

// Beinhaltet die Messwerte in byte
// [0] ID, [1] Sensor-Wert-ID, [2] Wert-Ganzzahl, [3] Wert-Nachkomma
uint8_t myDataArr[4];

// nRF24-Radio-Objekt definieren
RF24 radio(CE_PIN, CSN_PIN);

// Adressen für Sender und Empfänger
byte addresses[][6] = {"1Node","2Node"};

// Zähler zur "Verwaltung" der Interrupts
volatile int counter = 0;
volatile int counterLed = 0;
#define WAKEUP 112 // Anzahl der Interrupts, nach denen die Temp gemessen wird (einen Interrupt später wird gesendet)
#define LEDFLASH 4   // Anzahl der Interrupts nach denen die Keep-Alive LED blitzt

// Watchdog-Timer Interrupt
ISR(WDT_vect)
{
  // Zähle den Counter hoch, danach springt Programm in enter_sleep() nach Anweisung "sleep_mode()" und danach in den loop
  counter++;
  counterLed++;
}

// Funktion setzt alle Register, um den Sleep Mode einzuleiten
void enter_sleep(void) {
  
  byte adcsra_save = ADCSRA; // ADC-Status speichern

  ADCSRA &= ~_BV(ADEN);  // Deaktiviere A-D-Wander (spart ca. 300 mA) 
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Power Down Mode   
  power_all_disable(); // Deaktiviert Peripherie
  sleep_mode(); // Starte Sleep-Modus
  
  // 
  // Ab hier wird nach dem Schlafen weiter ausgeführt
  //
  sleep_disable();
  power_all_enable();     // Alle Komponenten aktivieren
  ADCSRA = adcsra_save; // ADC-Status zurücksetzen
}


void setup()
{
  // Definiere Pins, hier Status LED und zeige Setup mit kurzem Blinken an
  pinMode(LEDPIN,OUTPUT);
  digitalWrite(LEDPIN, HIGH);
  delay(500);
  digitalWrite(LEDPIN, LOW);

  // Initialisiere den Sensor
  sensors.begin();
  sensors.setResolution(tempDeviceAddress, res);
  sensors.setWaitForConversion(false); 

  // Setup des Watchdog Timers 
  MCUSR &= ~(1 << WDRF);                          // WDT reset flag loeschen 
  WDTCSR |= (1 << WDCE) | (1 << WDE);             // WDCE setzen, Zugriff auf Presclaler etc. 
  WDTCSR = 1 << WDP0 | 1 << WDP3;                 //  Prescaler/"Wake-up" auf 8.0 s 
  WDTCSR |= 1 << WDIE;                            // WDT Interrupt aktivieren
  
  // Starte Sender 
  radio.begin();
  // radio.setPALevel(RF24_PA_LOW);  // bei geringen Entfernungen reicht eine schwache Sendeintensität
  radio.setDataRate( RF24_250KBPS ); // langsamere Übertragung = weniger Stromverbrauch
  radio.setRetries (15,5);           // Versucht 5x mit Abstand 15 ms die Daten zu senden, falls keine Bestätigung vom Empfänger ankam 
  
  // Öffne die Pipes zum Senden und Empfangen
  radio.openWritingPipe(addresses[1]); // hier wird gesendet
  radio.openReadingPipe(1,addresses[0]); // hier wird empfangen

  // Deaktiviere Radio nach dem Senden zum Stromsparen
  radio.powerDown();    
  enter_sleep();
}


void loop()
{
  // Alle "LEDFLASH - Durchgänge" LED kurz blitzen lassen
  if (counterLed > LEDFLASH) {
    digitalWrite(LEDPIN, HIGH);
    delay(10);
    digitalWrite(LEDPIN, LOW);
    counterLed = 0;
  }
  
  // Wenn Temperatur vorher abgefragt wurde, ist nach max. 975 ms eine Temp verfügbar. Zwischen Abfrage und Verfügbarkeit schläft der Prozessor
  if (getTemp) {
    // Counter erneut beginnen
    counter = 0;
    
    // Temperatur muss erneut abgefragt werden
    getTemp = false;
    
    // Lasse LED blinken (Senden)
    // Kann zum Strom sparen gelöscht werden
    for (int i = 0; i <2; i++) {
      digitalWrite(LEDPIN, HIGH);
      delay(10);
      digitalWrite(LEDPIN, LOW);
      delay(30);
    }

    // Starte Radio
    radio.powerUp();
    
    // Hole Temperaturwert
    temperature = sensors.getTempCByIndex(0);

    // Setze Array
    myDataArr[0] = SensorID; // Sensor ID
    myDataArr[1] = MeasurementID; // Messwert-ID
    myDataArr[2] = (uint8_t) temperature; // Ganzzahl-Wert der Temperatur
    myDataArr[3] = (uint8_t) ((temperature-(uint8_t)temperature)*100); // Nachkomma-Wert der Temperatur

    // Sende Daten
    if (!radio.write( &myDataArr, sizeof(myDataArr) )){
      // Kommunikation fehlgeschlagen
      for (int i = 0; i < 5; i++) {
        digitalWrite(LEDPIN, HIGH);
        delay(10);
        digitalWrite(LEDPIN, LOW);
        delay(50);
      }
    }

    // Deaktiviere Radio nach dem Senden zum Stromsparen
    radio.powerDown();    
    digitalWrite(CSN_PIN,LOW);
  } 

  // Wenn Zeit für nächste Temperaturabfrage gekommen ist, dann durchführen
  // Beim nächsten Durchgang wird diese dann gesendet
  if (counter >= WAKEUP) {
    sensors.requestTemperatures();
    getTemp = true;
  }

  // gehe erneut schlafen
  enter_sleep();
}
