// On 2026-01-07 at 17:16:10, the setting key was modified. Long press to enter the time modification, and short press to enter the alarm time modification.
// Configured for ESP32 DevKit V1 (WROOM-32)
#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "esp32"  
#endif

// DS1302 RTC Module:
//   VCC → ESP32 5V (or 3.3V)
//   GND → ESP32 GND
//   CLK → ESP32 GPIO18
//   DAT → ESP32 GPIO23
//   RST → ESP32 GPIO5
// DFPlayer Mini Connections:
//   VCC → ESP32 5V
//   GND → ESP32 GND  
//   TX → ESP32 GPIO2 (MP3_RX_PIN)
//   RX → ESP32 GPIO4 (MP3_TX_PIN)
//   SPK_1 → Speaker Red (+)   
//   SPK_2 → Speaker Black (-) 

#if defined(ESP32)
  #ifdef ESP_IDF_VERSION_MAJOR // IDF 4+
    #if CONFIG_IDF_TARGET_ESP32 // ESP32/PICO-D4
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial1
      #define RADAR_RX_PIN 33
      #define RADAR_TX_PIN 32
      MONITOR_SERIAL.print("CONFIG_IxDF_TARGET_ESP32");
   
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
    #define RADAR_RX_PIN 33   //The RX1 pin of the ESP32 is connected to the TX pin of the radar
    #define RADAR_TX_PIN 32   //ESP32's TX1 is connected to the RX of the radar
  #endif
#elif defined(__AVR_ATmega32U4__)
  #define MONITOR_SERIAL Serial
  #define RADAR_SERIAL Serial1
  #define RADAR_RX_PIN 0
  #define RADAR_TX_PIN 1
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ld2410.h>
#include <DFRobotDFPlayerMini.h>
#include <PubSubClient.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include "arduino_secrets.h"

/********* WIFI *********/
const char* ssid = SECRET_SSID;
const char* pass = SECRET_PASS;

/********* MQTT *********/
static const char* MQTT_HOST = "mqtt.cetools.org";
static const int   MQTT_PORT = 1884;  
static const char* MQTT_CLIENTID = "ALARM_CLOCK_JRONG_ESP32";
const char* mqtt_topic_subscribe = "student/CASA0019/Junrong/pressurepad";

/********* MQTT Authentication *********/
static const char* MQTT_USER = MQTT_USERNAME;
static const char* MQTT_PASS = MQTT_PASSWORD;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// DS1302 RTC pins
#define RTC_CLK_PIN 18
#define RTC_DAT_PIN 23
#define RTC_RST_PIN 5

#define MP3_RX_PIN 4    //The RX2 pin of the ESP32 is connected to the TX pin of the MP3 (pin 3)
#define MP3_TX_PIN 2    //The TX2 pin of the ESP32 is connected to the RX pin (PIN2) of the MP3.

DFRobotDFPlayerMini mp3;
bool mp3Ready = false;

// Initialize DS1302
ThreeWire myWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN);
RtcDS1302<ThreeWire> rtc(myWire);

// U8g2 Constructor for ELEGOO 0.96" OLED (128x64, I2C) (ROTATE RESET SCL=22 SDA=21)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 22, 21);

// Button pins
#define BTN_SET 25  // Setting key: Switches menu options
#define BTN_UP 26   // Increase button: Click +1 / Long press for combo attack
#define BTN_DOWN 27 // Reduce key: Click -1 / Long press for combo attack

// Buzzer pin
#define BUZZER_PIN 13

// Button debounce
#define DEBOUNCE_DELAY 50
unsigned long lastDebounceTime[3] = {0, 0, 0};
bool lastButtonState[3] = {HIGH, HIGH, HIGH};
bool buttonState[3] = {HIGH, HIGH, HIGH};

// Pressure pad state (received via MQTT)
bool pressurePadPressed = false;
unsigned long lastPressureUpdate = 0;

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
bool wifiConnected = false;
bool mqttConnected = false;

unsigned long lastMQTTAttempt = 0;
const unsigned long MQTT_RETRY_INTERVAL = 5000;

bool setting_mode = false;      //Set mode flag (true = enter settings / false = normal display)

// Button shake reduction + combo parameters (industrial-grade configuration, no need for modification)
const int DEBOUNCE_TIME = 20;   // Shake-off time: 20 ms
const int LONG_PRESS_TIME = 500;// Long press to determine time: 500ms
const int REPEAT_INTERVAL = 200;// The interval between consecutive attacks is 200 milliseconds.


// 将设置参数写入DS1302，立即生效
void saveParamToRtc() {
  RtcDateTime initTime(2026, 1, 3, 9, 35, 0); // Year, month, day, hour, minute, second
  rtc.SetDateTime(initTime);
}

// ==================== MQTT Callback ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  MONITOR_SERIAL.print("📩 MQTT [");
  MONITOR_SERIAL.print(topic);
  MONITOR_SERIAL.print("]: ");
  MONITOR_SERIAL.println(message);
  
  // Check if it's from the pressure pad topic
  if (String(topic) == mqtt_topic_subscribe) {
    if (message == "pressed") {
      pressurePadPressed = true;
      lastPressureUpdate = millis();
      MONITOR_SERIAL.println("✓ Pressure pad: PRESSED");
    } else if (message == "released") {
      pressurePadPressed = false;
      lastPressureUpdate = millis();
      MONITOR_SERIAL.println("✓ Pressure pad: RELEASED");
    }
  }
}

// ==================== WiFi Setup ====================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  MONITOR_SERIAL.print("Connecting to WiFi: ");
  MONITOR_SERIAL.println(ssid);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  u8g2.setFont(u8g2_font_unifont_t_symbols); 
  u8g2.setFont(u8g2_font_open_iconic_all_1x_t); 
  u8g2.setFont(u8g2_font_7x13_t_symbols); 
  u8g2.drawStr(0, 15, "Connecting WiFi...");
  u8g2.sendBuffer();
  
  WiFi.begin(ssid, pass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    MONITOR_SERIAL.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    MONITOR_SERIAL.println("\n✓ WiFi Connected!");
    MONITOR_SERIAL.print("IP: ");
    MONITOR_SERIAL.println(WiFi.localIP());
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "WiFi Connected!");
    u8g2.sendBuffer();
    delay(2000);
  } else {
    wifiConnected = false;
    MONITOR_SERIAL.println("\n✗ WiFi Failed!");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "WiFi Failed!");
    u8g2.sendBuffer();
    delay(2000);
  }
}

// ==================== MQTT Setup ====================
void ensureMQTT() {
  if (mqtt.connected()) {
    mqttConnected = true;
    return;
  }
  
  if (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL) return;
  
  lastMQTTAttempt = millis();
  
  MONITOR_SERIAL.print("MQTT connect ");
  MONITOR_SERIAL.print(MQTT_HOST);
  MONITOR_SERIAL.print(":");
  MONITOR_SERIAL.println(MQTT_PORT);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 15, "Connecting MQTT...");
  u8g2.sendBuffer();
  
  bool ok = mqtt.connect(MQTT_CLIENTID, MQTT_USER, MQTT_PASS);
  
  if (ok) {
    mqttConnected = true;
    MONITOR_SERIAL.println("✓ MQTT Connected!");
    
    // Subscribe to pressure pad topic
    mqtt.subscribe(mqtt_topic_subscribe);
    MONITOR_SERIAL.print("Subscribed to: ");
    MONITOR_SERIAL.println(mqtt_topic_subscribe);
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "MQTT Connected!");
    u8g2.sendBuffer();
    delay(1000);
    
  } else {
    mqttConnected = false;
    MONITOR_SERIAL.print("MQTT fail rc=");
    MONITOR_SERIAL.println(mqtt.state());
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "MQTT Failed!");
    u8g2.sendBuffer();
  }
}
// 补零函数（格式化时间）
String formatNum(int num) {
  return num < 10 ? "0" + String(num) : String(num);
}

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
  Wire.begin(21, 22); // I2C initialization: SDA connected to 21, SCL connected to 22. For the other ESP32 I2C initialization, Wire.begin(18, 19);
  Wire.setClock(400000); // Increase the I2C speed to 400kHz (default is 100kHz)
  
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
  u8g2.drawStr(0, 50, "Init systems...");
  u8g2.sendBuffer();
  
  delay(1000);

  // Initialize WiFi
  connectWiFi();
  
  // Initialize MQTT
  if (wifiConnected) {
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    ensureMQTT();
  }

  // Initialize DS1302 RTC
  MONITOR_SERIAL.println("Initializing RTC Module...");
  MONITOR_SERIAL.print("  CLK: GPIO");
  MONITOR_SERIAL.println(RTC_CLK_PIN);
  MONITOR_SERIAL.print("  DAT: GPIO");
  MONITOR_SERIAL.println(RTC_DAT_PIN);
  MONITOR_SERIAL.print("  RST: GPIO");
  MONITOR_SERIAL.println(RTC_RST_PIN);
  
  rtc.Begin();
   // First use: Set the initial time
  RtcDateTime initTime(2026, 1, 3, 9, 35, 0); 
  if (!rtc.IsDateTimeValid()) {
    rtc.SetDateTime(initTime);
  	Serial.println("MH-DS1302 V2：Initialization time successful");
  } else {
     Serial.println("MH-DS1302 V2：The time is valid and does not need to be reset.");
  }

  if (!rtc.GetIsRunning()) {
    MONITOR_SERIAL.println("RTC was not running, starting now");
    rtc.SetIsRunning(true);
  }

  RtcDateTime now = rtc.GetDateTime();
  now = rtc.GetDateTime(); // Second reading, stable V2 version data
  rtcConnected = true;
  // Format the time
  static String date = String(now.Year()) + "/" + formatNum(now.Month()) + "/" + formatNum(now.Day());
  static String time = formatNum(now.Hour()) + ":" + formatNum(now.Minute()) + ":" + formatNum(now.Second());

  currentHour = now.Hour();
  currentMinute = now.Minute();
  currentSecond = now.Second();
  MONITOR_SERIAL.print(date);
  MONITOR_SERIAL.print(" Current RTC time: ");
  MONITOR_SERIAL.print(time);
  MONITOR_SERIAL.println(" ");  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 15, "Clock on!");
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
  MONITOR_SERIAL.printf("RADAR_RX_PIN=%d,RADAR_TX_PIN=%d  ", RADAR_RX_PIN, RADAR_TX_PIN);
  RADAR_SERIAL.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
#elif defined(__AVR_ATmega32U4__)
  RADAR_SERIAL.begin(256000);
#endif

  delay(500);

  bool ok = false;
  unsigned long start = millis();

  while (millis() - start < 3000) {
    if (!ok) {
      MONITOR_SERIAL.println("Calling radar.begin()...");
      ok = radar.begin(RADAR_SERIAL);
    }

    radar.read();

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
    MONITOR_SERIAL.println(F("Radar: FAILED"));

    u8g2.drawStr(0, 15, "Radar Error");
    u8g2.drawStr(0, 30, "Check wiring / reboot");
  }

  u8g2.sendBuffer();
  delay(2000);

  // --- Initialize MP3 player ---
  MONITOR_SERIAL.println("Initializing MP3 player...");
  MONITOR_SERIAL.print("  RX: GPIO");
  MONITOR_SERIAL.println(MP3_RX_PIN);
  MONITOR_SERIAL.print("  TX: GPIO");
  MONITOR_SERIAL.println(MP3_TX_PIN);

  Serial2.begin(9600, SERIAL_8N1, MP3_RX_PIN, MP3_TX_PIN);
  delay(2000);

  MONITOR_SERIAL.println("Attempting connection...");

  if (mp3.begin(Serial2)) {
    mp3Ready = true;
    MONITOR_SERIAL.println("✓ MP3 player OK");
    
    delay(500);
    mp3.volume(15);
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
    MONITOR_SERIAL.println("  - Power supply adequate (5V 2A)?");
  }

  // Show result on display
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  if (mp3Ready) {
    u8g2.drawStr(0, 15, "MP3: OK");
  } else {
    u8g2.drawStr(0, 15, "MP3: FAILED");
    u8g2.drawStr(0, 30, "Check Power/SD");
  }
  u8g2.sendBuffer();
  delay(2000);

  lastTimeUpdate = millis();
  
  MONITOR_SERIAL.println("\n=== SYSTEM READY ===");
  MONITOR_SERIAL.print("Display: ");
  MONITOR_SERIAL.println(displayConnected ? "OK" : "FAIL");
  MONITOR_SERIAL.print("RTC: ");
  MONITOR_SERIAL.println(rtcConnected ? "OK" : "FAIL");
  MONITOR_SERIAL.print("Radar: ");
  MONITOR_SERIAL.println(radarConnected ? "OK" : "FAIL");
  MONITOR_SERIAL.print("WiFi: ");
  MONITOR_SERIAL.println(wifiConnected ? "OK" : "FAIL");
  MONITOR_SERIAL.print("MQTT: ");
  MONITOR_SERIAL.println(mqttConnected ? "OK" : "FAIL");
  MONITOR_SERIAL.print("MP3: ");
  MONITOR_SERIAL.println(mp3Ready ? "OK" : "FAIL");
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
      now = rtc.GetDateTime();
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

    // Check alarm every second
    // Previously, the alarm was triggered when the time was exactly at "whole hours, whole minutes, and seconds equal to 0" (for example, if the alarm was set for 8:30, it would be triggered at 8:30:00). However, directly using this condition has three major issues: inaccurate triggering, missed triggering, and repeated triggering.
    // 1. Basic condition: Hours and minutes match (seconds are relaxed to 0-2, tolerating time synchronization/loop delay)
   bool timeMatch = (currentHour == alarmHour) && (currentMinute == alarmMinute) && (currentSecond >= 0 && currentSecond <= 2);
  // 2. Trigger condition: Time match + No trigger within this minute
  if (alarmEnabled && timeMatch && !alarmTriggered) {
  //   triggerAlarm(); // Execute the alarm logic (ringing / radar alert / sending MQTT, etc.)
    MONITOR_SERIAL.println("⏰ ALARM TRIGGERED!");
    alarmTriggered = true; // Mark as triggered to avoid repetition
  }

  // 3. Reset marker: After a minute change, it is allowed to trigger again (for example, from 8:30 → 8:31, reset the marker)
  if (currentMinute != alarmMinute) {
//    alarmTriggered = false;
  }

  }
}

void checkAlarm()
{
  // Alarm rings when BOTH conditions are met:
  // 1. Alarm is triggered (time reached)
  // 2. Person detected by radar OR pressure pad is pressed
  bool personDetected = (presenceDetected && targetDistance < 100) || pressurePadPressed;
  
  if (alarmTriggered && personDetected) {
    if (!alarmRinging) {
      MONITOR_SERIAL.println("🔊 ALARM RINGING - PERSON DETECTED!");
      if (pressurePadPressed) {
        MONITOR_SERIAL.println("   └─ Via pressure pad");
      }
      if (presenceDetected) {
        MONITOR_SERIAL.println("   └─ Via radar");
      }
     
      
      // Play MP3 track
      if (mp3Ready && !alarmRinging) {
        mp3.play(1);          // Play the specified track first
        delay(50);              // Wait for the playback command to take effect (short delay, non-blocking alternative available)
        mp3.volume(30);
        mp3.loop(1);  // Play first track on repeat
        alarmRinging = true;
      }

     }
    tone(BUZZER_PIN, 4000, 200);  // Keep buzzer as backup
  } else {
    if (alarmRinging) {
      alarmRinging = false;
      alarmTriggered = false;
      noTone(BUZZER_PIN);
      
  // 4. 停止循环播放
      // Stop MP3 playback
      if (mp3Ready) {
        mp3.stop();             
        mp3.loop(0);            // cancel loop
        alarmRinging = false;
      }
    }
  }
  
  // // Reset alarm after the minute passes
  // if (alarmTriggered && currentSecond == 0 && currentMinute != alarmMinute) {
  //   alarmTriggered = false;
  //   alarmRinging = false;
  //   if (mp3Ready) mp3.stop();
  //   MONITOR_SERIAL.println("Alarm reset");
  // }
}

void handleButtons()
{

  // 1. SET button: Press briefly to enter the setting mode, press for a long time (>800ms) to save and exit the setting.
  static unsigned long set_press_time = 0;
  if (digitalRead(BTN_SET) == LOW) {
    delay(DEBOUNCE_TIME);
    if (digitalRead(BTN_SET) == LOW) {
      set_press_time = millis();
      while (digitalRead(BTN_SET) == LOW) delay(10); // Wait for the button to be released
      // Determine long/short press
      if (millis() - set_press_time > 800) {
        // Long press: Save settings + Exit setting mode
        if (setting_mode) {
 
  //        saveParamToRtc(); // Write the parameters to the DS1302
         // Save time to RTC
	        if (rtcConnected) {
	          RtcDateTime now = rtc.GetDateTime();
	          RtcDateTime newTime(now.Year(), now.Month(), now.Day(),
	                             currentHour, currentMinute, 0);
	          rtc.SetDateTime(newTime);
	        }

          setting_mode = false;
          Serial.println("✅ Saved successfully! Exit the settings mode.");
        }else {
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
		        MONITOR_SERIAL.println("Menu: Set Alarm Hour");
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
      } else {
        // Short press: Switch menu options (enter settings / move to the next item)
        setting_mode = true;
        Serial.print("🔄 切换菜单 → ");
   {
    if (alarmRinging) {
      // Can only stop alarm if person is detected
      bool personDetected = (presenceDetected && targetDistance < 100) || pressurePadPressed;
      if (personDetected) {
        alarmTriggered = false;
        alarmRinging = false;
        noTone(BUZZER_PIN);
        if (mp3Ready) mp3.stop();
        MONITOR_SERIAL.println("✓ Alarm stopped - person verified");
      } else {
        MONITOR_SERIAL.println("❌ Cannot stop alarm - no person detected!");
      }
      return;
    }
    
    switch(currentMenu) {
      case MENU_CLOCK: 
        currentMenu = MENU_SET_ALARM_HOUR;
        MONITOR_SERIAL.println("Menu: Set Alarm Hour");
        break;
      case MENU_SET_HOUR: 
        currentMenu = MENU_SET_MINUTE;
        MONITOR_SERIAL.println("Menu: Set Minute");
        break;
      case MENU_SET_MINUTE: 
        currentMenu = MENU_SET_ALARM_HOUR;
        MONITOR_SERIAL.println("Menu: Set Alarm Hour");
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
     }
    }
  }
  
  
   // 2. Increase key UP: Click +1 / Long press for consecutive +1s
  static unsigned long up_last_time = 0;
  if (digitalRead(BTN_UP) == LOW) {
		delay(DEBOUNCE_TIME);
		if (digitalRead(BTN_UP) == LOW) {
			if (millis() - up_last_time >= REPEAT_INTERVAL) {
//        paramAdd(); // Perform the addition by 1 operation
  {
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
        up_last_time = millis();
      }
    }
  } else {
    up_last_time = millis() - REPEAT_INTERVAL;
  }
 	
   // 3. Reduce key DOWN: Single click -1 / Long press for combo -1
  static unsigned long down_last_time = 0;
	if (digitalRead(BTN_DOWN) == LOW && setting_mode) {
    delay(DEBOUNCE_TIME);
    if (digitalRead(BTN_DOWN) == LOW) {
      if (millis() - down_last_time >= REPEAT_INTERVAL) {
 //       paramSub(); // Perform the subtraction operation of 1
   {
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
       down_last_time = millis();
      }
    }
  } else {
    down_last_time = millis() - REPEAT_INTERVAL;
  }
 
}

void updateDisplay()
{
  static char str[30];
  
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
  
  // Show pressure pad OR radar status
  if (pressurePadPressed&&(presenceDetected && targetDistance < 100)) {
    sprintf(str, "Press:Yes|Present:Yes");
  } else if (pressurePadPressed&&!(presenceDetected && targetDistance < 100)) {
    sprintf(str, "Press:Yes|Present:No");
  } else if (!pressurePadPressed&&!(presenceDetected && targetDistance < 100)) {
    sprintf(str, "Press:No|Present:No");
  } else if (!pressurePadPressed&& (presenceDetected && targetDistance < 100)) {
    sprintf(str, "Press:No|Present:Yes");
  }
  u8g2.drawStr(0, 52, str);
  
  switch(currentMenu) {
    case MENU_CLOCK:
      // Show MQTT and MP3 status on main screen
      if (mqttConnected && mp3Ready) {
        u8g2.drawStr(0, 63, "MQTT:OK | MP3:OK");
      } else if (!mqttConnected && mp3Ready) {
        u8g2.drawStr(0, 63, "MQTT:OFF | MP3:OK");
      } else if (!mqttConnected && !mp3Ready) {
        u8g2.drawStr(0, 63, "MQTT:OFF | MP3:OFF");
      } else if (mqttConnected && !mp3Ready) {
        u8g2.drawStr(0, 63, "MQTT:OK | MP3:OFF");
      }
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
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  // Maintain MQTT connection
  ensureMQTT();
  
  // Process MQTT messages
  mqtt.loop();
  
  updateTime();
  radar.read(); // First, trigger the reception of the underlying data from the radar (for some radars, the read() function needs to be called first before the status is checked)
  
  // Optimized radar data reading conditions
  unsigned long currentMillis = millis();
  // 1. First, determine the time interval (fixing overflow + sequence optimization), and then determine the radar connection.
  if ((currentMillis - lastReading >= 500) && radar.isConnected()) {
    // Immediately update the timing to avoid repeated reading caused by multi-threading or loop cycles.
    lastReading = currentMillis;
  //if (radar.isConnected() && millis() - lastReading > 500)
  //{
   // lastReading = millis();
    
   // Read the core data of the radar
    // Raw presence from radar
    bool rawPresence = radar.presenceDetected();
    int distance = 0;
     
      // Static target distance: Add validity verification (to avoid outliers)
    if (rawPresence)
    {
      if (radar.stationaryTargetDetected())
      {
        int stationaryDist = radar.stationaryTargetDistance();
        if (stationaryDist >= 0 && stationaryDist <= 500) { // Maximum detection range limit of radar
          distance = stationaryDist;
        }    
      }

        // Distance of moving target: Preferentially take the closer valid distance
      if (radar.movingTargetDetected())
      {
        int md = radar.movingTargetDistance();
        if (distance == 0 || md < distance) {
          distance = md;
        }
      }
    }

    targetDistance = distance;

    // Redefine "presenceDetected" to mean "person closer than 100cm"
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
