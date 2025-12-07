// DS1302 RTC Module:
//   VCC → ESP32 5V (or 3.3V)
//   GND → ESP32 GND
//   CLK → ESP32 GPIO18
//   DAT → ESP32 GPIO23
//   RST → ESP32 GPIO5
// DFPlayer Mini Connections:
//   VCC → ESP32 5V
//   GND → ESP32 GND  
//   TX → ESP32 GPIO16 (MP3_RX_PIN)
//   RX → ESP32 GPIO17 (MP3_TX_PIN)
//   SPK_1 → Speaker Red (+)   
//   SPK_2 → Speaker Black (-) 

#if defined(ESP32)
  #ifdef ESP_IDF_VERSION_MAJOR // IDF 4+
    #if CONFIG_IDF_TARGET_ESP32 // ESP32/PICO-D4
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial1
      #define RADAR_RX_PIN 33
      #define RADAR_TX_PIN 32
    #elif CONFIG_IDF_TARGET_ESP32S2
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial1
      #define RADAR_RX_PIN 9
      #define RADAR_TX_PIN 8
    #elif CONFIG_IDF_TARGET_ESP32C3
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial1
      #define RADAR_RX_PIN 4
      #define RADAR_TX_PIN 5
    #else 
      #error Target CONFIG_IDF_TARGET is not supported
    #endif
  #else // ESP32 Before IDF 4.0
    #define MONITOR_SERIAL Serial
    #define RADAR_SERIAL Serial1
    #define RADAR_RX_PIN 33
    #define RADAR_TX_PIN 32
  #endif
#elif defined(__AVR_ATmega32U4__)
  #define MONITOR_SERIAL Serial
  #define RADAR_SERIAL Serial1
  #define RADAR_RX_PIN 0
  #define RADAR_TX_PIN 1
#endif

#include <ld2410.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <DFRobotDFPlayerMini.h>






// DS1302 RTC pins
#define RTC_CLK_PIN 18
#define RTC_DAT_PIN 23
#define RTC_RST_PIN 5

#define MP3_RX_PIN 4
#define MP3_TX_PIN 2

DFRobotDFPlayerMini mp3;
bool mp3Ready = false;

// Initialize DS1302
ThreeWire myWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN);
RtcDS1302<ThreeWire> rtc(myWire);

// U8g2 Constructor for ELEGOO 0.96" OLED (128x64, I2C)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 22, 21);



// Button pins
#define BTN_SET 25
#define BTN_UP 26
#define BTN_DOWN 27

// Buzzer pin
#define BUZZER_PIN 13

// Button debounce
#define DEBOUNCE_DELAY 50
unsigned long lastDebounceTime[3] = {0, 0, 0};
bool lastButtonState[3] = {HIGH, HIGH, HIGH};
bool buttonState[3] = {HIGH, HIGH, HIGH};

// Menu states
enum MenuState {
  MENU_CLOCK,
  MENU_SET_HOUR,
  MENU_SET_MINUTE,
  MENU_SET_ALARM_HOUR,
  MENU_SET_ALARM_MINUTE,
  MENU_ALARM_ENABLE
};

MenuState currentMenu = MENU_CLOCK;

// Time variables
int currentHour = 12;
int currentMinute = 0;
int currentSecond = 0;
unsigned long lastTimeUpdate = 0;

// Alarm variables
int alarmHour = 7;
int alarmMinute = 0;
bool alarmEnabled = false;
bool alarmTriggered = false;
bool alarmRinging = false;

ld2410 radar;
uint32_t lastReading = 0;
bool radarConnected = false;
bool presenceDetected = false;
int targetDistance = 0;

bool displayConnected = false;
bool rtcConnected = false;

void setup(void)
{
  MONITOR_SERIAL.begin(115200);
  delay(1000);
  MONITOR_SERIAL.println("\n--- Un-Snoozeable Alarm Clock ---");
  
  // Setup buttons
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // Setup buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  MONITOR_SERIAL.println("Initializing I2C for OLED...");
  Wire.begin(21, 22); // SDA=21, SCL=22

  MONITOR_SERIAL.println("Initializing OLED Display...");

  // Initialize display
  u8g2.begin();
  u8g2.enableUTF8Print();
  displayConnected = true;
  
  MONITOR_SERIAL.println("Display initialized!");
  
  // Welcome screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(0, 20, "HELLO!");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 35, "Alarm Clock");
  u8g2.drawStr(0, 50, "Init Clock module...");
  u8g2.sendBuffer();
  
  delay(1000);

  // Initialize DS1302 RTC
  MONITOR_SERIAL.println("Initializing DS1302 RTC Module...");
  MONITOR_SERIAL.print("  CLK: GPIO");
  MONITOR_SERIAL.println(RTC_CLK_PIN);
  MONITOR_SERIAL.print("  DAT: GPIO");
  MONITOR_SERIAL.println(RTC_DAT_PIN);
  MONITOR_SERIAL.print("  RST: GPIO");
  MONITOR_SERIAL.println(RTC_RST_PIN);
  
  rtc.Begin();
  
  // Just check if RTC is running, don't force set time
  if (!rtc.GetIsRunning()) {
    MONITOR_SERIAL.println("RTC was not running, starting now");
    rtc.SetIsRunning(true);
  }

  // Read current time from RTC (no forced setting)
  RtcDateTime now = rtc.GetDateTime();
  
  rtcConnected = true;
  
  // Load current time from RTC
  currentHour = now.Hour();
  currentMinute = now.Minute();
  currentSecond = now.Second();
  
  MONITOR_SERIAL.print("Current RTC time: ");
  MONITOR_SERIAL.print(currentHour);
  MONITOR_SERIAL.print(":");
  MONITOR_SERIAL.print(currentMinute);
  MONITOR_SERIAL.print(":");
  MONITOR_SERIAL.println(currentSecond);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 15, "Clock Active!");
  u8g2.setFont(u8g2_font_6x10_tr);
  char timeStr[20];
  sprintf(timeStr, "Time: %02d:%02d:%02d", currentHour, currentMinute, currentSecond);
  u8g2.drawStr(0, 35, timeStr);
  u8g2.sendBuffer();
  delay(2000);

  // ---------- Initialize Radar ----------
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 15, "Starting Radar...");
  u8g2.sendBuffer();

#if defined(ESP32)
  MONITOR_SERIAL.println("Initializing Radar UART...");
  RADAR_SERIAL.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
#elif defined(__AVR_ATmega32U4__)
  RADAR_SERIAL.begin(256000);
#endif

  delay(500);   // give UART + sensor some time

  bool ok = false;
  unsigned long start = millis();

  // Try radar.begin and wait for data / connection for up to 3 seconds
  while (millis() - start < 3000) {
    if (!ok) {
      MONITOR_SERIAL.println("Calling radar.begin()...");
      ok = radar.begin(RADAR_SERIAL);
    }

    radar.read();  // try to parse any incoming frames

    if (radar.isConnected()) {
      ok = true;
      break;
    }

    delay(100);
  }

  radarConnected = ok;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  if (radarConnected) {
    MONITOR_SERIAL.println(F("Radar: OK"));

    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 15, "RADAR OK!");
    u8g2.setFont(u8g2_font_6x10_tr);

    char fwStr[20];
    sprintf(fwStr, "FW v%d.%d.%02X",
            radar.firmware_major_version,
            radar.firmware_minor_version,
            radar.firmware_bugfix_version);
    u8g2.drawStr(0, 30, fwStr);
  } else {
    MONITOR_SERIAL.println(F("Radar: FAILED (no connection)"));

    u8g2.drawStr(0, 15, "Radar Error");
    u8g2.drawStr(0, 30, "Check wiring / reboot");
  }

  u8g2.sendBuffer();
  delay(2000);






  // --- Initialize MP3 player (DFPlayer Mini compatible) ---
  MONITOR_SERIAL.println("Initializing MP3 player...");
  MONITOR_SERIAL.println("  TX");
  MONITOR_SERIAL.println("  RX");

  Serial2.begin(9600, SERIAL_8N1, MP3_RX_PIN, MP3_TX_PIN);
  delay(2000);  // ← INCREASE from 500ms to 2000ms

  MONITOR_SERIAL.println("Attempting connection...");

  if (mp3.begin(Serial2)) {
    mp3Ready = true;
    MONITOR_SERIAL.println("✓ MP3 player OK");
    
    delay(500);
    mp3.volume(30);  // Max volume for testing
    delay(500);
    
    // Test play
    MONITOR_SERIAL.println("Testing playback...");
    mp3.play(1);
    delay(3000);
    mp3.stop();
    
  } else {
    mp3Ready = false;
    MONITOR_SERIAL.println("❌ MP3 player NOT found");
    MONITOR_SERIAL.println("Check:");
    MONITOR_SERIAL.println("  - SD card inserted?");
    MONITOR_SERIAL.println("  - 0001.mp3 exists?");
    MONITOR_SERIAL.println("  - Wiring correct?");
    MONITOR_SERIAL.println("  - 1k resistor on RX?");
  }

  // Show result on display
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  if (mp3Ready) {
    u8g2.drawStr(0, 15, "MP3: OK");
  } else {
    u8g2.drawStr(0, 15, "MP3: FAILED");
    u8g2.drawStr(0, 30, "Check SD/Wiring");
  }
  u8g2.sendBuffer();
  delay(2000);
}

bool isButtonPressed(int buttonIndex, int buttonPin)
{
  bool reading = digitalRead(buttonPin);
  
  if (reading != lastButtonState[buttonIndex]) {
    lastDebounceTime[buttonIndex] = millis();
  }
  
  if ((millis() - lastDebounceTime[buttonIndex]) > DEBOUNCE_DELAY) {
    if (reading != buttonState[buttonIndex]) {
      buttonState[buttonIndex] = reading;
      
      if (buttonState[buttonIndex] == LOW) {
        lastButtonState[buttonIndex] = reading;
        return true;
      }
    }
  }
  
  lastButtonState[buttonIndex] = reading;
  return false;
}

void updateTime()
{
  if (millis() - lastTimeUpdate >= 1000) {
    lastTimeUpdate += 1000;

    if (rtcConnected && currentMenu == MENU_CLOCK) {
      // Normal mode: follow RTC
      RtcDateTime now = rtc.GetDateTime();
      currentHour   = now.Hour();
      currentMinute = now.Minute();
      currentSecond = now.Second();
    } else {
      // In setting menus (or no RTC): let time tick locally
      currentSecond++;
      if (currentSecond >= 60) {
        currentSecond = 0;
        currentMinute++;
        if (currentMinute >= 60) {
          currentMinute = 0;
          currentHour = (currentHour + 1) % 24;
        }
      }
    }

    // Check alarm every second using currentHour/currentMinute/currentSecond
    if (alarmEnabled && !alarmTriggered) {
      if (currentHour == alarmHour && currentMinute == alarmMinute && currentSecond == 0) {
        alarmTriggered = true;
        MONITOR_SERIAL.println("⏰ ALARM TRIGGERED!");
      }
    }
  }
}


void checkAlarm()
{
  if (alarmTriggered && presenceDetected && targetDistance < 100) {
    if (!alarmRinging) {
      MONITOR_SERIAL.println("🔊 ALARM RINGING - PERSON DETECTED!");
      alarmRinging = true;
      
      // ADD THIS: Play MP3 track #1 on loop
      if (mp3Ready) {
        mp3.loop(1);  // Play first track on repeat
        // OR use: mp3.play(1); to play once
      }
    }
    tone(BUZZER_PIN, 4000);  // Keep buzzer as backup
  } else {
    if (alarmRinging) {
      alarmRinging = false;
      noTone(BUZZER_PIN);
      
      // ADD THIS: Stop MP3 playback
      if (mp3Ready) {
        mp3.stop();
      }
      MONITOR_SERIAL.println("Alarm stopped - no person detected");
    }
  }
  
  if (alarmTriggered && currentSecond == 0 && currentMinute != alarmMinute) {
    alarmTriggered = false;
    alarmRinging = false;
    if (mp3Ready) mp3.stop();  // ADD THIS
    MONITOR_SERIAL.println("Alarm reset");
  }
}

void handleButtons()
{
  if (isButtonPressed(0, BTN_SET)) {
    if (alarmRinging) {
      alarmTriggered = false;
      alarmRinging = false;
      noTone(BUZZER_PIN);
      MONITOR_SERIAL.println("Alarm stopped by user");
      return;
    }
    
    switch(currentMenu) {
      case MENU_CLOCK: 
        currentMenu = MENU_SET_HOUR;
        MONITOR_SERIAL.println("Menu: Set Hour");
        break;
      case MENU_SET_HOUR: 
        currentMenu = MENU_SET_MINUTE;
        MONITOR_SERIAL.println("Menu: Set Minute");
        break;
      case MENU_SET_MINUTE: 
        currentMenu = MENU_SET_ALARM_HOUR;
        // Save time to RTC
        if (rtcConnected) {
          RtcDateTime now = rtc.GetDateTime();
          RtcDateTime newTime(now.Year(), now.Month(), now.Day(),
                             currentHour, currentMinute, 0);
          rtc.SetDateTime(newTime);
          MONITOR_SERIAL.println("✓ Time saved to RTC");
        }
        break;
      case MENU_SET_ALARM_HOUR: 
        currentMenu = MENU_SET_ALARM_MINUTE;
        MONITOR_SERIAL.println("Menu: Set Alarm Minute");
        break;
      case MENU_SET_ALARM_MINUTE: 
        currentMenu = MENU_ALARM_ENABLE;
        MONITOR_SERIAL.println("Menu: Alarm Enable");
        break;
      case MENU_ALARM_ENABLE: 
        currentMenu = MENU_CLOCK;
        MONITOR_SERIAL.println("Menu: Clock");
        break;
    }
  }
  
  if (isButtonPressed(1, BTN_UP)) {
    switch(currentMenu) {
      case MENU_SET_HOUR: currentHour = (currentHour + 1) % 24; break;
      case MENU_SET_MINUTE: currentMinute = (currentMinute + 1) % 60; currentSecond = 0; break;
      case MENU_SET_ALARM_HOUR: alarmHour = (alarmHour + 1) % 24; break;
      case MENU_SET_ALARM_MINUTE: alarmMinute = (alarmMinute + 1) % 60; break;
      case MENU_ALARM_ENABLE: 
        alarmEnabled = !alarmEnabled; 
        alarmTriggered = false;
        MONITOR_SERIAL.print("Alarm: ");
        MONITOR_SERIAL.println(alarmEnabled ? "ON" : "OFF");
        break;
    }
  }
  
  if (isButtonPressed(2, BTN_DOWN)) {
    switch(currentMenu) {
      case MENU_SET_HOUR: currentHour = (currentHour - 1 + 24) % 24; break;
      case MENU_SET_MINUTE: currentMinute = (currentMinute - 1 + 60) % 60; currentSecond = 0; break;
      case MENU_SET_ALARM_HOUR: alarmHour = (alarmHour - 1 + 24) % 24; break;
      case MENU_SET_ALARM_MINUTE: alarmMinute = (alarmMinute - 1 + 60) % 60; break;
      case MENU_ALARM_ENABLE: 
        alarmEnabled = !alarmEnabled; 
        alarmTriggered = false;
        MONITOR_SERIAL.print("Alarm: ");
        MONITOR_SERIAL.println(alarmEnabled ? "ON" : "OFF");
        break;
    }
  }
}

void updateDisplay()
{
  char str[30];
  
  u8g2.clearBuffer();
  
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 8, "Un-Snoozeable");
  u8g2.drawLine(0, 11, 127, 11);
  
  u8g2.setFont(u8g2_font_ncenB14_tr);
  sprintf(str, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);
  u8g2.drawStr(5, 30, str);
  
  u8g2.setFont(u8g2_font_6x10_tr);
  sprintf(str, "Alarm:%02d:%02d %s", alarmHour, alarmMinute, alarmEnabled ? "[ON]" : "[OFF]");
  u8g2.drawStr(0, 42, str);
  
  if (presenceDetected) {
    //sprintf(str, "Person:YES %dcm", targetDistance);
    sprintf(str, "Person:YES");
  } else {
    sprintf(str, "Person:NO");
  }
  u8g2.drawStr(0, 52, str);
  
  switch(currentMenu) {
    case MENU_CLOCK:
      u8g2.drawStr(0, 63, "Alarm Clock");
      break;
    case MENU_SET_HOUR:
      sprintf(str, "Set Clock Hour:%d", currentHour);
      u8g2.drawStr(0, 63, str);
      break;
    case MENU_SET_MINUTE:
      sprintf(str, "Set Clock Min:%d", currentMinute);
      u8g2.drawStr(0, 63, str);
      break;
    case MENU_SET_ALARM_HOUR:
      sprintf(str, "Alarm Hr:%d", alarmHour);
      u8g2.drawStr(0, 63, str);
      break;
    case MENU_SET_ALARM_MINUTE:
      sprintf(str, "Alarm Min:%d", alarmMinute);
      u8g2.drawStr(0, 63, str);
      break;
    case MENU_ALARM_ENABLE:
      sprintf(str, "Alarm:%s", alarmEnabled ? "ON" : "OFF");
      u8g2.drawStr(0, 63, str);
      break;
  }
  
  if (alarmRinging) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 128, 11);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(15, 9, "WAKE UP!!!");
    u8g2.setDrawColor(1);
  }
  
  u8g2.sendBuffer();
}

void loop()
{
  static unsigned long lastTest = 0;
  
  // // Force play every 5 seconds
  // if (millis() - lastTest > 5000) {
  //   lastTest = millis();
  //   if (mp3Ready) {
  //     Serial.println("Force playing track 1...");
  //     mp3.play(1);
  //   }
  // }

  updateTime();
  radar.read();
  
  if (radar.isConnected() && millis() - lastReading > 500)
  {
    lastReading = millis();
    
    // Raw presence from radar
    bool rawPresence = radar.presenceDetected();
    int distance = 0;

    if (rawPresence)
    {
      if (radar.stationaryTargetDetected())
      {
        distance = radar.stationaryTargetDistance();
      }

      if (radar.movingTargetDetected())
      {
        int md = radar.movingTargetDistance();
        if (distance == 0 || md < distance) {
          distance = md;
        }
      }
    }

    targetDistance = distance;

    // HERE: redefine "presenceDetected" to mean "person closer than 100cm"
    if (rawPresence && targetDistance > 0 && targetDistance < 100) {
      presenceDetected = true;
    } else {
      presenceDetected = false;
    }
  }

  
  handleButtons();
  checkAlarm();
  updateDisplay();
  
  delay(50);
}