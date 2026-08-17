#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 1. Konfigurasi OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 2. Pin I2C ESP32 & Pin Sensor AD8232
#define SDA_PIN 33 
#define SCL_PIN 32 

const int pinSensor = 36; // VP (GPIO 36)
const int pinLOM    = 27; // D27 (LO-)
const int pinLOP    = 26; // D26 (LO+)

// 3. Pin LED Indikator
const int ledHijau  = 14; 
const int ledKuning = 12; 
const int ledMerah  = 13; 

// 4. Variabel Global Pemrosesan Sinyal & Grafis
int xPos = 0;
int yPosLama = 40;
float dynamicBaseline = 2048.0; 

float emgEnvelope = 0.00;
int currentState = 1; // 1: Hijau (Rileks), 2: Kuning (Tegang), 3: Merah (Mengepal)
unsigned long previousMillis = 0;
const long interval = 300; // Output Serial Monitor setiap 300ms

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(pinLOM, INPUT);
  pinMode(pinLOP, INPUT);
  pinMode(ledHijau, OUTPUT);
  pinMode(ledKuning, OUTPUT);
  pinMode(ledMerah, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED Gagal Diinisialisasi!"));
    for (;;); 
  }

  // Tampilan Kalibrasi Awal
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println(F("KALIBRASI ECG..."));
  display.setCursor(10, 35);
  display.println(F("RILEKSKAN TANGAN"));
  display.display();

  // Autokalibrasi Baseline Awal (Titik Nol Offset 1.65V)
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += analogRead(pinSensor);
    delay(10);
  }
  dynamicBaseline = sum / 100.0;

  display.clearDisplay();
  display.setCursor(20, 25);
  display.println(F("EMG MODE READY"));
  display.display();
  delay(1000);
  display.clearDisplay();
}

void loop() {
  // A. DETEKSI ELEKTRODA TERLEPAS
  if ((digitalRead(pinLOM) == HIGH) || (digitalRead(pinLOP) == HIGH)) {
    digitalWrite(ledHijau, LOW);
    digitalWrite(ledKuning, LOW);
    digitalWrite(ledMerah, LOW);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(25, 25);
    display.println(F("PAD TERLEPAS!"));
    display.display();
    return;
  }

  // B. SAMPLING SENSOR & DYNAMIC BASELINE ADAPTIF
  int sensorValue = analogRead(pinSensor);
  
  // Dynamic baseline meluruhkan drift offset dengan sangat halus
  dynamicBaseline = (dynamicBaseline * 0.998) + (sensorValue * 0.002);
  
  int selisihADC = abs(sensorValue - (int)dynamicBaseline);

  // Deadzone filter noise mikro (< 15 ADC disaring)
  if (selisihADC < 15) selisihADC = 0;

  // Konversi skala ADC ke tegangan mV
  float instantEMG = (map(selisihADC, 0, 800, 0, 500) / 100.0);
  instantEMG = constrain(instantEMG, 0.00, 5.00);

  // C. ALGORITMA ENVELOPE DENGAN PEAK-HOLD DINAMIS
  if (instantEMG > emgEnvelope) {
    emgEnvelope = (emgEnvelope * 0.4) + (instantEMG * 0.6); // Respon cepat saat otot berkontraksi
  } else {
    emgEnvelope = (emgEnvelope * 0.85) + (instantEMG * 0.15); // Peluruhan lembut saat dirilekskan
  }
  
  if (emgEnvelope < 0.02) emgEnvelope = 0.02;

  // D. OUTPUT SERIAL MONITOR (Setiap 300 ms)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    Serial.print("EMG Otot: ");
    Serial.print(emgEnvelope, 2);
    Serial.println(" mV");
  }

  // E. LOGIKA HISTERESIS LED (BEBAS KEDAP-KEDIP)
  if (currentState == 1) { 
    // Naik ke Kuning hanya jika sinyal meyakinkan (>= 0.18 mV)
    if (emgEnvelope >= 0.18) currentState = 2; 
  } 
  else if (currentState == 2) { 
    // Naik ke Merah jika mengepal kuat (>= 0.28 mV)
    if (emgEnvelope >= 0.28) currentState = 3; 
    // Hanya kembali ke Hijau jika sinyal benar-benar rileks (< 0.05 mV)
    else if (emgEnvelope < 0.05) currentState = 1; 
  } 
  else if (currentState == 3) { 
    // Turun ke Kuning jika tekanan mengepal mengendur (< 0.20 mV)
    if (emgEnvelope < 0.20) currentState = 2; 
  }

  // Eksekusi Lampu LED Berdasarkan State
  digitalWrite(ledHijau,  currentState == 1 ? HIGH : LOW);
  digitalWrite(ledKuning, currentState == 2 ? HIGH : LOW);
  digitalWrite(ledMerah,  currentState == 3 ? HIGH : LOW);

  // F. DISPLAY OLED (Teks Nilai mV + Grafik Zoom Dynamic)
  display.fillRect(0, 0, 128, 16, SSD1306_BLACK); 
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("EMG: "));
  display.print(emgEnvelope, 2); 
  display.print(F(" mV"));

  // Scaling Grafik Layar
  int minVal = (int)dynamicBaseline - 200;
  int maxVal = (int)dynamicBaseline + 200;
  
  int yPos = map(sensorValue, minVal, maxVal, 63, 18);
  yPos = constrain(yPos, 18, 63);

  display.drawFastVLine(xPos, 18, 46, SSD1306_BLACK);

  if (xPos > 0) {
    display.drawLine(xPos - 1, yPosLama, xPos, yPos, SSD1306_WHITE);
  }
  display.display();

  yPosLama = yPos;
  xPos++;

  if (xPos >= SCREEN_WIDTH) {
    xPos = 0;
  }

  delay(15);
}