#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- KONFIGURASI WIFI & MQTT ---
const char* ssid = "yui123";      
const char* password = "12345678"; // Pastikan Password Benar
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

// --- PIN DEFINITION ---
const int pinLedTeras = 13;
const int pinLedTamu = 12;
const int pinLedTidur = 14;
const int pinLedDapur = 25; 

const int pinMotorIN1 = 26;
const int pinMotorIN2 = 27;

#define PIN_DHT 4
#define DHTTYPE DHT22

// --- SETTING KECEPATAN MOTOR ---
// REKOMENDASI: Jangan di bawah 90.
// Kalau 70 terlalu lemah, motor akan "Ngeden" (Stall) dan bikin ESP32 restart.
int motorSpeed = 90; 

// OBJEK
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2); 
DHT dht(PIN_DHT, DHTTYPE);

// VARIABEL LOGIKA
unsigned long lastTempUpdate = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long gateTimer = 0; 
bool isIdle = true; 
float suhu = 0;
String statusPagar = "TUTUP";

void setup() {
  // Matikan deteksi Brownout (Biar gak restart pas tegangan turun dikit)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  // Kasih waktu booting hardware
  delay(2000); 
  Serial.begin(115200);

  // --- SETUP MOTOR ---
  ledcAttach(pinMotorIN1, 5000, 8);
  ledcAttach(pinMotorIN2, 5000, 8);
  ledcWrite(pinMotorIN1, 0);
  ledcWrite(pinMotorIN2, 0);

  // --- SETUP LAMPU ---
  ledcAttach(pinLedTeras, 5000, 8);
  ledcAttach(pinLedTamu, 5000, 8);
  ledcAttach(pinLedTidur, 5000, 8);
  ledcAttach(pinLedDapur, 5000, 8);
  // Matikan semua lampu dulu
  ledcWrite(pinLedTeras, 0);
  ledcWrite(pinLedTamu, 0);
  ledcWrite(pinLedTidur, 0);
  ledcWrite(pinLedDapur, 0);

  // --- SETUP HARDWARE ---
  // Konfigurasi I2C Manual biar Stabil (SDA=21, SCL=22)
  Wire.begin(21, 22);
  Wire.setClock(100000); // Turunkan speed I2C ke 100kHz (Standard) biar gak error

  dht.begin();
  lcd.init();
  lcd.backlight();
  
  // --- SETUP WIFI & MQTT ---
  setupWifi(); // Di sini titik krusialnya
  
  // Trik Anti-Crash: Delay sebentar setelah WiFi konek
  // Biar tegangan stabil dulu sebelum LCD mulai kerja berat
  delay(500); 

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Intro
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("SYSTEM READY!");
  delay(1500);
  tampilIdle(); 
}

void loop() {
  // 1. CEK WIFI 
  if (WiFi.status() != WL_CONNECTED) {
     setupWifi();
     return;
  }

  // 2. CEK MQTT
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

  // 3. TAMPILAN STANDBY (Setelah 5 detik)
  if (!isIdle && (now - gateTimer > 5000)) {
    isIdle = true; 
    lcd.clear();   
    tampilIdle();  
  }

  // 4. UPDATE SUHU
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
  
  // Trik Anti-WDT: Beri nafas sedikit untuk CPU
  delay(10); 
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

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  Serial.print("Msg: "); Serial.println(message);
  
  int nilaiPWM = message.toInt(); 

  // LOGIKA LAMPU
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
      lcd.setCursor(0, 1); lcd.print("Membuka...");
      
      // START SOFT
      ledcWrite(pinMotorIN1, motorSpeed); 
      ledcWrite(pinMotorIN2, 0);          
      
      delay(1000); // Durasi buka
      
      // STOP
      ledcWrite(pinMotorIN1, 0);
      ledcWrite(pinMotorIN2, 0);
      
      lcd.setCursor(0, 1); lcd.print("Terbuka!      ");
      statusPagar = "BUKA";
    }
    else if (message == "TUTUP") {
      lcd.setCursor(0, 0); lcd.print("Bye-Bye!");
      lcd.setCursor(0, 1); lcd.print("Menutup...");
      
      // START SOFT
      ledcWrite(pinMotorIN1, 0);          
      ledcWrite(pinMotorIN2, motorSpeed); 
      
      delay(1000); // Durasi tutup
      
      // STOP
      ledcWrite(pinMotorIN1, 0);
      ledcWrite(pinMotorIN2, 0);
      
      lcd.setCursor(0, 1); lcd.print("Tertutup!     ");
      statusPagar = "TUTUP";
    }
  }
}

// SETUP WIFI (Dengan Retry)
void setupWifi() {
  delay(10);
  Serial.print("Konek WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    // Kalau 20 detik gak konek, coba restart modul wifi
    if(retry > 40) {
        Serial.println("\nWiFi Timeout. Restarting...");
        ESP.restart();
    }
  }
  Serial.println("\nConnected!");
}

boolean reconnect() {
  String clientId = "ESP32-KSL-" + String(random(0xffff), HEX);
  if (client.connect(clientId.c_str())) {
    client.subscribe("proyekiot/lampu/#");
    client.subscribe("proyekiot/gerbang");
    return true;
  }
  return false;
}
