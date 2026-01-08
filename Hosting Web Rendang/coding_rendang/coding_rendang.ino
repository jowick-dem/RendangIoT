#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

//KONFIGURASI
const char* ssid = "IoT Rendang";    
const char* password = "Depong27";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

//PIN
const int pinLedTeras = 13;
const int pinLedTamu = 12;
const int pinLedTidur = 14;
const int pinLedDapur = 25; 
const int pinMotorIN1 = 26;
const int pinMotorIN2 = 27;

#define PIN_DHT 4
#define DHTTYPE DHT22

//OBJEK
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2); 
DHT dht(PIN_DHT, DHTTYPE);

//VARIABEL LOGIKA
unsigned long lastTempUpdate = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long gateTimer = 0; 
bool isIdle = true; 
float suhu = 0;
String statusPagar = "TUTUP";

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  delay(1000); // Delay LCD
  Serial.begin(115200);

  // Setup Dinamo
  pinMode(pinMotorIN1, OUTPUT);
  pinMode(pinMotorIN2, OUTPUT);

  //PWM LED
  ledcAttach(pinLedTeras, 5000, 8);
  ledcAttach(pinLedTamu, 5000, 8);
  ledcAttach(pinLedTidur, 5000, 8);
  ledcAttach(pinLedDapur, 5000, 8);

  // Matikan Lampu 
  ledcWrite(pinLedTeras, 0);
  ledcWrite(pinLedTamu, 0);
  ledcWrite(pinLedTidur, 0);
  ledcWrite(pinLedDapur, 0);

  // Setup Hardware 
  dht.begin();
  lcd.init();
  lcd.backlight();
  
  // Setup WiFi & MQTT
  setupWifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Intro
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("SYSTEM READY!");
  delay(1500);
  tampilIdle(); 
}

void loop() {
  //KONEKSI 
  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnect()) lastReconnectAttempt = 0;
    }
  } else {
    client.loop();
  }

  unsigned long now = millis();

  // TAMPILAN AWAL (delay 5 detik)
  if (!isIdle && (now - gateTimer > 5000)) {
    isIdle = true; 
    lcd.clear();   
    tampilIdle();  
  }

  // UPDATE SUHU (Mode Idle)
  if (isIdle && (now - lastTempUpdate > 3000)) {
    lastTempUpdate = now;
    float t = dht.readTemperature();
    if (!isnan(t)) {
      suhu = t;
      char tempString[8];
      dtostrf(suhu, 1, 1, tempString);
      if(client.connected()) client.publish("proyekiot/sensor/suhu", tempString);
      tampilIdle();
    }
  }
}

// TAMPILAN UTAMA
void tampilIdle() {
  lcd.setCursor(0, 0);
  lcd.print("Rumah Rendang   "); 
  
  lcd.setCursor(0, 1);
  lcd.print("Suhu: "); 
  lcd.print((int)suhu);
  lcd.write(0xDF); 
  lcd.print("C       "); 
}

// FUNGSI CALLBACK 
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  Serial.print("Msg: "); Serial.println(message);
  
  // Konversi pesan 
  int nilaiPWM = message.toInt(); 

  // LOGIKA LAMPU (DIMMER)
  if (String(topic) == "proyekiot/lampu/teras") ledcWrite(pinLedTeras, nilaiPWM);
  else if (String(topic) == "proyekiot/lampu/tamu") ledcWrite(pinLedTamu, nilaiPWM);
  else if (String(topic) == "proyekiot/lampu/tidur") ledcWrite(pinLedTidur, nilaiPWM);
  else if (String(topic) == "proyekiot/lampu/dapur") ledcWrite(pinLedDapur, nilaiPWM);

  // LOGIKA GERBANG 
  if (String(topic) == "proyekiot/gerbang") {
    isIdle = false; 
    gateTimer = millis(); 
    lcd.clear(); 

    if (message == "BUKA") {
      lcd.setCursor(0, 0); lcd.print("WELCOME");
      lcd.setCursor(0, 1); lcd.print("Gerbang di Buka");
      digitalWrite(pinMotorIN1, HIGH);
      digitalWrite(pinMotorIN2, LOW);
      delay(2000); 
      digitalWrite(pinMotorIN1, LOW);
      digitalWrite(pinMotorIN2, LOW);
      statusPagar = "BUKA";
    }
    else if (message == "TUTUP") {
      lcd.setCursor(0, 0); lcd.print("Bye-Bye!");
      lcd.setCursor(0, 1); lcd.print("Gerbang di Tutup");
      digitalWrite(pinMotorIN1, LOW);
      digitalWrite(pinMotorIN2, HIGH);
      delay(1500); 
      digitalWrite(pinMotorIN1, LOW);
      digitalWrite(pinMotorIN2, LOW);
      statusPagar = "TUTUP";
    }
  }
}

// KONEKSI WIFI 
void setupWifi() {
  Serial.print("Konek WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if(WiFi.status() == WL_CONNECTED) Serial.println("Connected!");
}

boolean reconnect() {
  String clientId = "ESP32-" + String(random(0xffff), HEX);
  if (client.connect(clientId.c_str())) {
    client.subscribe("proyekiot/lampu/#");
    client.subscribe("proyekiot/gerbang");
    return true;
  }
  return false;
}
