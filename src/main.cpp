// By Fatih Nurrobi Alansshori Teknik Komputer UPI

#include <Arduino.h>

#define RELAY1 1
#define RELAY2 2
#define RELAY3 41
#define RELAY4 42
#define RELAY5 45
#define RELAY6 46

void isibak();
void mixing();
void supply();
void relayoff();

void setup() {
Serial.begin(115200);
pinMode(RELAY1, OUTPUT);  
pinMode(RELAY2, OUTPUT);
pinMode(RELAY3, OUTPUT);
pinMode(RELAY4, OUTPUT);
pinMode(RELAY5, OUTPUT);
pinMode(RELAY6, OUTPUT);
}

void loop() {
  isibak();
  delay(2000);
  mixing();
  delay(2000);
  supply();
  delay(2000);
  relayoff();
  delay(2000);
}

// put function definitions here:
void isibak(){
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, LOW);
  digitalWrite(RELAY4, HIGH);
  digitalWrite(RELAY5, LOW);
}

void mixing(){
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, HIGH);
  digitalWrite(RELAY5, LOW);
}

void supply(){
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, LOW);
  digitalWrite(RELAY5, HIGH);
}

void relayoff(){
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW); 
  digitalWrite(RELAY3, LOW);
  digitalWrite(RELAY4, LOW);
  digitalWrite(RELAY5, LOW);
}
