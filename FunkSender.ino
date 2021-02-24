/*
 * FunkSender
 * 
 * V2.0, 21.02.2021 
 * 
 * MIT License
 * 
 * Copyright (c) 2020 jhka
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Kondensatoren:
 *                           x-----(opt. 220 uF -------x
 *                           x---------  10 uF --------x
 *                           x--------- 100 nF --------x
 *                           |          _____          | 
 *  nRF24L01  VCC, pin2 --- VCC       1|o A  |14      GND --- NTC & nRF24L01 GND, pin1 
 *                          PB0 (10)  2|  T  |13 (A0) AREF--- NTC & R
 *  LED (R 100 Ohm)         PB1 ( 9)  3|  t  |12 ( 1) PA1 --- 
 *                          PB3 (  )  4|  i  |11 ( 2) PA2 --- 
 *  nRF24L01 CE, pin4       PB2 ( 8)  5|  n  |10 ( 3) PA3 --- R POWER  
 *  nRF24L01 CSN, pin3      PA7 ( 7)  6|  y  |9  ( 4) PA4 --- nRF24L01 SCK, pin5
 *  nRF24L01 MISO, pin7 --- PA6 ( 6)  7|_____|8  ( 5) PA5 --- nRF24L01 MOSI, pin6
 *  
 *                                ^                ^
 *                                |  Arduino Pin#  |
 */


// Sleep-Funktionen
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

// Achtung - neueste RF24 Library von TMRh20 verwenden
#include "RF24.h"

// Eindeutige ID des Senders definieren
#define     SensorID      123 // 1 byte (0..255)
#define     MeasurementID 1 // ID für verschiedene Messungen (inkl. Erweiterungen): 1: Temperatur, 2: Luftfeuchtigkeit, 3: Bodenfeuchtigkeit, 4: Öffnungsschalter, 5: Spannung, ...

// Definierte PINS
#define     NTCPIN        A0
#define     NTCPWR        3
#define     LEDPIN        9
#define     CE_PIN        8
#define     CSN_PIN       7

// NTC-Parameter
#define     R_OBEN        10000.0 // oberer Widerstand fest
#define     R_N           10000.0 // unterer Widerstand NTC (bei 25°)
#define     T_N           298.15  // bei T = 25°C = 298,15°K
#define     B_WERT        3450.0  // B-Wert des NTC
float RWert;                      // berechneter Widerstandswert des NTC
float wertSpannungsteiler;        // gelesener Wert am Spannungsteiler (0..1023)
float temperatur = 0.0;           // Speichert Temperatur

// Beinhaltet die Messwerte in byte
// [0] ID, [1] Sensor-Wert-ID, [2] Wert-Ganzzahl, [3] Wert-Nachkomma
int8_t myDataArr[4];

// nRF24-Radio-Objekt definieren
RF24 radio(CE_PIN, CSN_PIN);

// Adressen für Sender und Empfänger
byte addresses[][6] = {"1Node","2Node"};

// Zähler zur "Verwaltung" der Interrupts
// Zu Beginn direkt mit dem nächsten Aufwachen den ersten Datensatz senden
#define     WAKEUP        112 // Anzahl der Interrupts, nach denen die Temp gemessen wird 
#define     LEDFLASH      8   // Anzahl der Interrupts nach denen die Keep-Alive LED blitzt hier: 8x8 Sekunden
volatile int counter = WAKEUP;
volatile int counterLed = LEDFLASH;

// Watchdog-Timer Interrupt
ISR(WDT_vect)
{
  // Zähle den Counter hoch, danach springt Programm in enter_sleep() auf Zeile nach der Anweisung "sleep_mode()" und danach in den loop
  counter++;
  counterLed++;
}

// Funktion setzt alle Register, um den Sleep Mode einzuleiten
void enter_sleep(void) {
  
  byte adcsra_save = ADCSRA; // ADC-Status speichern

  ADCSRA &= ~_BV(ADEN);  // Deaktiviere A-D-Wander (spart ca. 300 uA) 
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
  pinMode(LEDPIN, OUTPUT);
  pinMode(NTCPIN, INPUT);
  pinMode(NTCPWR, OUTPUT);
  digitalWrite(NTCPWR, LOW);
  digitalWrite(LEDPIN, HIGH);
  delay(500);
  digitalWrite(LEDPIN, LOW);

  // Setup des Watchdog Timers 
  MCUSR &= ~(1 << WDRF);                          // WDT reset flag loeschen 
  WDTCSR |= (1 << WDCE) | (1 << WDE);             // WDCE setzen, Zugriff auf Presclaler etc. 
  WDTCSR = 1 << WDP0 | 1 << WDP3;                 //  Prescaler/"Wake-up" auf 8.0 s 
  WDTCSR |= 1 << WDIE;                            // WDT Interrupt aktivieren
  
  // Starte Sender 
  if (radio.begin()) {
    radio.setChannel(101);  // Kanal, auf dem gesendet wird (zwischen 1 und 125), untere Kanäle oft von WIFI belegt
    radio.setPayloadSize(sizeof(myDataArr)); // Sende nur so viele Daten, wie nötig (4 byte für SensorID, DatenID, Vorkommastelle, Nachkommastelle
    radio.setAutoAck(true); // Automatische Bestätigung bei Empfang Daten
    radio.setPALevel(RF24_PA_MAX);  // bei geringen Entfernungen reicht eine schwache Sendeintensität (RF24_PA_LOW) ansonsten RF24_PA_MAX oder RF24_PA_HIGH
    radio.setDataRate(RF24_1MBPS); // langsamere Übertragung (RF24_250kbps) = weniger Stromverbrauch & größere Reichweite aber nicht Kompatibel mit RF24L01 (ohne '+')
    radio.setRetries (15,5);           // Versucht 5x mit Abstand 4 ms die Daten zu senden, falls keine Bestätigung vom Empfänger ankam 
    
    // Öffne die Pipes zum Senden und Empfangen diese sind definiert durch die Adresse im array (5 byte)
    radio.openWritingPipe(addresses[1]); // hier wird gesendet
    radio.openReadingPipe(1,addresses[0]); // hier wird empfangen
    radio.stopListening(); // versetze Sender in "Sendemodus"
    
    // Deaktiviere Radio nach dem Senden zum Stromsparen
    radio.powerDown();  
  }  
  // Falls Sender nicht erkannt bzw. initialisiert wurde, signalisiere mit LED
  else {
    // Kommunikation fehlgeschlagen
    for (int i = 0; i < 50; i++) {
      digitalWrite(LEDPIN, HIGH);
      delay(100);
      digitalWrite(LEDPIN, LOW);
      delay(200);
    }
  }
}


void loop()
{
  // Alle "LEDFLASH - Durchgänge" LED kurz blitzen lassen
  if (counterLed >= LEDFLASH) {
    digitalWrite(LEDPIN, HIGH);
    delay(10);
    digitalWrite(LEDPIN, LOW);
    counterLed = 0;
  }
  
  // Wenn Zeit für nächste Temperaturabfrage gekommen ist, dann durchführen
  if (counter >= WAKEUP) {
    // Counter erneut beginnen
    counter = 0;

    // Setze Array
    myDataArr[0] = SensorID; // Sensor ID
    myDataArr[1] = MeasurementID; // Messwert-ID

    // Messe den Wert am Spannungsteiler 
    digitalWrite (NTCPWR, HIGH); // Spannungsteiler "aktivieren"    
    delay(3); // Warten zum Stabilisieren

    wertSpannungsteiler = analogRead(NTCPIN); // Wert ist Divisor, darf deswegen nicht 0 werden (NTC kurzgeschlossen)
    digitalWrite (NTCPWR, LOW); // Spannungsteiler ausschalten, um Strom zu sparen

    // Wenn der Messwert < 100 oder > 1000 ist, dann liegt ein Fehler vor (entspräche > 95°C oder < -40°C)
    if (wertSpannungsteiler > 100 && wertSpannungsteiler < 1000) {
      // Berechnet Widerstand des NTC aus dem gemessenen Verhältnis des Spannungsteilers
      RWert = R_OBEN/((1024/wertSpannungsteiler)-1);  
  
      // Temperatur wird anhand der Formel berechnet: https://learn.adafruit.com/thermistor/using-a-thermistor
      // Hierbei ist der B-Wert des NTC entscheidend.
      // Rechnung ist eine hinreichend genaue Annäherung der Temperatur
      temperatur = (1.0/ (log(RWert/R_N)/B_WERT + 1.0/298.15)) - 273.15;

      myDataArr[2] = (int8_t) temperatur; // Ganzzahl-Wert der Temperatur
      myDataArr[3] = (int8_t) (((int)abs(temperatur*10))%10); // 1 Nachkommastelle der Temperatur (hinreichend genau)
    }
    // im Fehlerfall werden beide Temperaturwerte als '-1' gesendet
    else {
      myDataArr[2] = 255; // Fehlerwert
      myDataArr[3] = 255; // Fehlerwert
    }

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
  } 
  // gehe erneut schlafen
  enter_sleep();
}
