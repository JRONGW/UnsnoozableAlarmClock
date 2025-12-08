#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <arduino_secrets.h>
/********* WIFI *********/
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

/********* MQTT *********/
static const char* MQTT_HOST = "mqtt.cetools.org";
static const int   MQTT_PORT = 1884;  
static const char* MQTT_CLIENTID = "PRESSURE_PAD_JRONG_ESP32";
const char* mqtt_topic = "student/CASA0019/Junrong/pressurepad";

/********* MQTT Authentication *********/
static const char* MQTT_USER = "student";
static const char* MQTT_PASS = "ce2021-mqtt-forget-whale";

/********* Hardware *********/
// ESP32 I2C pins: SDA=21, SCL=22 (default)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 22, 21);

const int MAT_PIN = 14;  // GPIO14 for pressure mat
const int LED_PIN = 2;   // ESP32 built-in LED

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool matPressed = false;
bool lastMatPressed = false;

unsigned long lastConnectionAttempt = 0;
const unsigned long RECONNECT_INTERVAL_MS = 5000;

void setup() {
  Serial.begin(115200);  // ESP32 typically uses 115200
  delay(2000);
  
  pinMode(MAT_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  Serial.println("=== ESP32 Pressure Mat MQTT Publisher ===");
  
  // Initialize I2C for display
  Wire.begin(21, 22);  // SDA=21, SCL=22
  
  // Initialize display
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(0, 20, "HELLO!");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 35, "Pressure Sensor");
  u8g2.drawStr(0, 50, "ESP32 Version");
  u8g2.sendBuffer();
  delay(2000);
  
  // Connect to WiFi
  connectWiFi();
  
  // Setup MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  
  Serial.println("Setup complete!");
}

void connectWiFi() {
  // Early return if already connected
  if (WiFi.status() == WL_CONNECTED) return;
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 15, "Connecting WiFi...");
  u8g2.sendBuffer();
  
  WiFi.begin(ssid, pass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "WiFi Connected!");
    
    char ipStr[20];
    sprintf(ipStr, "%d.%d.%d.%d", 
            WiFi.localIP()[0], 
            WiFi.localIP()[1], 
            WiFi.localIP()[2], 
            WiFi.localIP()[3]);
    u8g2.drawStr(0, 30, ipStr);
    u8g2.sendBuffer();
    delay(2000);
  } else {
    Serial.println("\n✗ WiFi Failed!");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "WiFi Failed!");
    u8g2.sendBuffer();
    delay(2000);
  }
}

void ensureMQTT() {
  // Early return if already connected
  if (mqtt.connected()) return;
  
  // Don't try too often
  if (millis() - lastConnectionAttempt < RECONNECT_INTERVAL_MS) return;
  
  lastConnectionAttempt = millis();
  
  Serial.print("MQTT connect ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 15, "Connecting MQTT...");
  u8g2.sendBuffer();
  
  // Connect WITH authentication
  bool ok = mqtt.connect(MQTT_CLIENTID, MQTT_USER, MQTT_PASS);
  
  if (ok) {
    Serial.println("✓ MQTT Connected!");
    Serial.print("Client ID: ");
    Serial.println(MQTT_CLIENTID);
    Serial.print("Topic: ");
    Serial.println(mqtt_topic);
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "MQTT Connected!");
    u8g2.sendBuffer();
    delay(1000);
    
    // Send initial state
    const char* initialState = matPressed ? "pressed" : "released";
    mqtt.publish(mqtt_topic, initialState, true);
    
    Serial.print("Initial state: ");
    Serial.println(initialState);
    
  } else {
    Serial.print("MQTT fail rc=");
    Serial.println(mqtt.state());
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "MQTT Failed!");
    char errorStr[20];
    sprintf(errorStr, "rc=%d", mqtt.state());
    u8g2.drawStr(0, 30, errorStr);
    u8g2.sendBuffer();
  }
}

void loop() {
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  // Maintain MQTT connection (non-blocking)
  ensureMQTT();
  
  // Process MQTT messages
  mqtt.loop();
  
  // Read mat state
  matPressed = (digitalRead(MAT_PIN) == LOW);
  
  // Visual feedback (blink LED when pressed)
  digitalWrite(LED_PIN, matPressed ? HIGH : LOW);
  
  // Update display
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 20, "Mat Status:");
  
  u8g2.setFont(u8g2_font_ncenB14_tr);
  if (matPressed) {
    u8g2.drawStr(10, 45, "PRESSED");
  } else {
    u8g2.drawStr(5, 45, "RELEASED");
  }
  
  // Show connection status
  u8g2.setFont(u8g2_font_6x10_tr);
  if (mqtt.connected()) {
    u8g2.drawStr(0, 63, "MQTT: OK");
  } else {
    u8g2.drawStr(0, 63, "MQTT: Connecting...");
  }
  
  u8g2.sendBuffer();
  
  // Publish only when state changes
  if (matPressed != lastMatPressed) {
    const char* status = matPressed ? "pressed" : "released";
    
    if (mqtt.publish(mqtt_topic, status, true)) {
      Serial.print("📤 Published: ");
      Serial.println(status);
    } else {
      Serial.println("❌ Publish failed");
    }
    
    lastMatPressed = matPressed;
  }
  
  delay(100);
}