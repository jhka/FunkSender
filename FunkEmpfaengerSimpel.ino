/*
 * FunkEmpfänger
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
 *   
 */
 
 // Achtung - neueste RF24 Library von TMRh20 verwenden
#include "RF24.h"

#define CE_PIN        8
#define CSN_PIN       7

// Beinhaltet die Messwerte in byte
// // Sensor-Wert-ID: 1: Temperatur, 2: Luftfeuchtigkeit, 3: Bodenfeuchtigkeit, 4: Öffnungsschalter, 5: Spannung, ...
// [0] ID, [1] Sensor-Wert-ID, [2] Wert-Ganzzahl, [3] Wert-Nachkomma
int8_t myDataArr[4];

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
    radio.setDataRate(RF24_1MBPS); // langsamere Übertragung (RF24_250kbps) = weniger Stromverbrauch & größere Reichweite aber nicht Kompatibel mit RF24L01 (ohne '+')
    radio.setPayloadSize(sizeof(myDataArr));
    radio.setChannel(101);  // Kanal, auf dem gesendet wird (zwischen 1 und 125), untere Kanäle oft von WIFI belegt
    radio.setAutoAck(true); // Automatische Bestätigung bei Empfang Daten
    radio.setPALevel(RF24_PA_MAX);  // bei geringen Entfernungen reicht eine schwache Sendeintensität ansonsten RF24_PA_MAX oder RF24_PA_HIGH
  
    // Öffne die Pipes zum Senden und Empfangen
    // Achtung, anders als beim Sender!
    radio.openWritingPipe(addresses[0]); // hier wird gesendet
    radio.openReadingPipe(1,addresses[1]); // hier wird empfangen
   
    // Starte den Empfang
    radio.startListening();
  }
  else {
    Serial.println("nicht erfolgreich");
  }

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

    // Wenn Nachkommastelle = "-1", dann hat der Sender einen Fehler gesendet
    if (myDataArr[3] == -1) {
      Serial.println("Fehler im Temp.-Sender");
    }
    else {
      Serial.print(myDataArr[2]);    
      Serial.print(",");
      Serial.println(myDataArr[3]);      
    }
  }
  delay(10);
}
