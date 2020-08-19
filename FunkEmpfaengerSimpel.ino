/*
 * FunkEmpfänger
 * 
 * V1.0, 19.08.2020
 *  
 */
 
 // Achtung - neueste RF24 Library von TMRh20 verwenden
#include "RF24.h"

#define CE_PIN        8
#define CSN_PIN       7

// Beinhaltet die Messwerte in byte
// // Sensor-Wert-ID: 1: Temperatur, 2: Luftfeuchtigkeit, 3: Bodenfeuchtigkeit, 4: Öffnungsschalter, 5: Spannung, ...
// [0] ID, [1] Sensor-Wert-ID, [2] Wert-Ganzzahl, [3] Wert-Nachkomma
uint8_t myDataArr[4];

// nRF24-Radio-Objekt definieren
RF24 radio(CE_PIN, CSN_PIN);

// Adressen für Sender und Empfänger
byte addresses[][6] = {"1Node","2Node"};

bool radioInit = false;

void setup() {

  Serial.begin(115200);

  Serial.println();

  Serial.print("Verbinde nRF24L01: ");
  if (radio.begin()){
    radioInit = true;
    Serial.println("erfolgreich");
  }
  else {
    Serial.println("nicht erfolgreich");
  }
  
  radio.setDataRate( RF24_250KBPS );

  // Öffne die Pipes zum Senden und Empfangen
  // Achtung, anders als beim Sender!
  radio.openWritingPipe(addresses[0]); // hier wird gesendet
  radio.openReadingPipe(1,addresses[1]); // hier wird empfangen
 
  // Starte den Empfang
  radio.startListening();
}

void loop() {
  // Wenn Daten vorhanden sind
  if (radioInit && radio.available() ) {
    // Lese alle Daten in das Array ein
    radio.read( &myDataArr, sizeof(myDataArr) );

    Serial.print("Sensor ID: ");
    Serial.print(myDataArr[0]);
    Serial.print("(WertID: ");
    Serial.print(myDataArr[1]);    
    Serial.print(") Wert: ");
    Serial.print(myDataArr[2]);    
    Serial.print(",");
    Serial.println(myDataArr[3]);
  }
  delay(10);
}
