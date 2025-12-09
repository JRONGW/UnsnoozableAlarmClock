#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"

static const uint8_t PIN_MP3_TX = 11;
static const uint8_t PIN_MP3_RX = 10;
SoftwareSerial softwareSerial(PIN_MP3_RX, PIN_MP3_TX);

DFRobotDFPlayerMini player;
void setup() {
  Serial.begin(9600);
  softwareSerial.begin(9600);
  
  Serial.println("Initializing DFPlayer...");
  
  if (!player.begin(softwareSerial)) {
    Serial.println("Failed to initialize!");
    while(true);
  }
  
  Serial.println("DFPlayer Mini online.");
  delay(1000);  // ← Important!
  
  player.volume(2);
  delay(500);  // ← ADD THIS - wait after volume command
  
  Serial.println("Playing track 1");
  player.play(1);
}


void loop() {
  if (player.available()) {
    uint8_t type = player.readType();
    int value = player.read();
    
    Serial.print("Type: ");
    Serial.print(type);
    Serial.print(", Value: ");
    Serial.println(value);
    
    // Decode the message
    if (type == 0x3D) { // Card inserted
      Serial.println("SD card inserted");
    } else if (type == 0x3C) { // Card removed
      Serial.println("SD card removed!");
    } else if (type == 0x40) { // Error
      Serial.println("ERROR!");
    }
  }
}