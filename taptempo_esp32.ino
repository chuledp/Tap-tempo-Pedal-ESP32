/*
 * CEREBRO MIDI ESP32-S3 - Branch: Web-Macros
 * V2.0 - Tap Tempo Híbrido + OLED + Web Server + LittleFS
 */

#include <Arduino.h>
#include <math.h> 
#include <Adafruit_NeoPixel.h> 
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "animation.h"
#include "web_interface.h"

// --- PINES ESP32-S3 ---
const int txPin = 43;      // MIDI OUT (Puenteado)
const int pedalPin = 13;   // Tap Tempo / Sustain
const int ledPin = 39;     // LED Externo
const int rgbPin = 48;     // NeoPixel
const int potPins[4] = {4, 5, 6, 7}; // ADC1 (Macros)

// --- RED Y SERVIDOR ---
const char* ssid = "Cerebro_MIDI";
const char* password = "arturia_freak";
AsyncWebServer server(80);

// --- CONFIGURACIÓN OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- CONFIGURACIÓN RGB ---
Adafruit_NeoPixel pixels(1, rgbPin, NEO_GRB + NEO_KHZ800);
uint32_t colorTempo  = pixels.Color(0, 0, 50);
uint32_t colorFlash  = pixels.Color(50, 50, 255);
uint32_t colorStart  = pixels.Color(255, 0, 0);
uint32_t colorListen = pixels.Color(180, 0, 255);
bool startMode = false; 

// --- VARIABLES RELOJ Y PEDAL ---
float currentBPM = 120.0;
unsigned long lastClockMicros = 0;
unsigned long clockIntervalMicros = 20833; 
int clockTickCounter = 0;

unsigned long pressStartTime = 0;
bool isPressed = false;
bool longPressHandled = false;
unsigned long lastPressTime = 0; 
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50; 

long tapIntervals[3]; 
int tapIndex = 0;
bool newSequence = true; 
int tapsInSequence = 0;  
long lastValidInterval = 500; 
const unsigned long TIMEOUT_MS = 2000;
bool isListeningMode = false;

// --- ESTRUCTURAS DE MACROS ---
struct Destino { int cc; int polarity; int min; int max; };
struct Macro { char name[7]; Destino dests[3]; };

Macro currentMacros[4];
int lastRawValue[4] = {-1, -1, -1, -1};
int lastSentValue[4][3];

// === FUNCIONES AUXILIARES ===

void sendResetSequence() {
  Serial1.write(0xFC); // STOP
  delay(5);
  Serial1.write(0xB0); Serial1.write(123); Serial1.write(0); // ALL NOTES OFF
  delay(5);
  Serial1.write(0xFA); // START
}

void reproducirAnimacionInicio() {
  int frameDelay = 1000 / 8; // 8 FPS
  for (int i = 0; i < epd_bitmap_allArray_LEN; i++) {
    display.clearDisplay();
    display.drawBitmap(0, 0, epd_bitmap_allArray[i], 128, 64, SSD1306_WHITE);
    display.display();
    delay(frameDelay); 
  }
  delay(500); 
  display.clearDisplay();
  display.display();
}

void cargarPreset(int absoluteNumber) {
  String path = "/preset_" + String(absoluteNumber) + ".json";
  if (!LittleFS.exists(path)) return;

  File file = LittleFS.open(path, "r");
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, file);
  file.close();

  for (int i = 0; i < 4; i++) {
    String mKey = "macro_" + String(i + 1);
    strlcpy(currentMacros[i].name, doc["macros"][mKey]["name"] | "MACRO", 7);
    for (int j = 0; j < 3; j++) {
      currentMacros[i].dests[j].cc = doc["macros"][mKey]["destinations"][j]["cc"] | -1;
      currentMacros[i].dests[j].polarity = doc["macros"][mKey]["destinations"][j]["polarity"] | 1;
      currentMacros[i].dests[j].min = doc["macros"][mKey]["destinations"][j]["min"] | 0;
      currentMacros[i].dests[j].max = doc["macros"][mKey]["destinations"][j]["max"] | 127;
    }
  }
}

void procesarMacros() {
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(potPins[i]);
    
    // Filtro de ruido (Hysteresis) para no saturar el bus MIDI
    if (abs(raw - lastRawValue[i]) < 15) continue; 
    lastRawValue[i] = raw;

    for (int j = 0; j < 3; j++) {
      Destino &d = currentMacros[i].dests[j];
      if (d.cc == -1) continue;

      int midiVal;
      if (d.polarity == 1) {
        midiVal = map(raw, 0, 4095, d.min, d.max);
      } else {
        midiVal = map(raw, 0, 4095, d.max, d.min);
      }
      
      // Limitar valores al estándar MIDI
      if (midiVal > 127) midiVal = 127;
      if (midiVal < 0) midiVal = 0;

      if (midiVal != lastSentValue[i][j]) {
        Serial1.write(0xB0); 
        Serial1.write(d.cc);
        Serial1.write(midiVal);
        lastSentValue[i][j] = midiVal;
      }
    }
  }
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  Serial1.begin(31250, SERIAL_8N1, -1, txPin);
  
  pinMode(pedalPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear(); pixels.show();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    pixels.setPixelColor(0, pixels.Color(255, 0, 0)); pixels.show();
    for(;;); 
  }

  reproducirAnimacionInicio();
  
  // File System
  if (!LittleFS.begin(true)) { Serial.println("Error LittleFS"); }

  // Wi-Fi Access Point
  WiFi.softAP(ssid, password);

  // Servidor Web
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "OK");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, data);
    int presetNum = doc["absolute_preset"];
    String path = "/preset_" + String(presetNum) + ".json";
    
    File file = LittleFS.open(path, "w");
    serializeJson(doc, file);
    file.close();
    
    cargarPreset(presetNum); 
  });

  server.begin();
  
  for(int i=0; i<3; i++) tapIntervals[i] = 500; 
  sendResetSequence();
  cargarPreset(1); // Carga default
}

// === LOOP (TAP TEMPO Y LECTURA) ===
void loop() {
  unsigned long nowMicros = micros();
  unsigned long nowMillis = millis();

  // 1. Escanear Potenciómetros y Enviar CC
  procesarMacros();

  // 2. Lógica Tap Tempo
  if (tapsInSequence > 0 && (nowMillis - lastPressTime < TIMEOUT_MS)) {
    isListeningMode = true;
  } else {
    isListeningMode = false;
    if (tapsInSequence > 0) {
      tapsInSequence = 0;
      newSequence = true;
    }
  }

  if (nowMicros - lastClockMicros >= clockIntervalMicros) {
    lastClockMicros += clockIntervalMicros;
    Serial1.write(0xF8);
    
    if (startMode) {
      digitalWrite(ledPin, HIGH); 
      pixels.setPixelColor(0, colorStart); pixels.show();
    } else if (isListeningMode) {
       digitalWrite(ledPin, LOW); 
       pixels.setPixelColor(0, colorListen); pixels.show();
    } else {
      if (clockTickCounter == 0) { 
        digitalWrite(ledPin, HIGH);        
        pixels.setPixelColor(0, colorFlash); pixels.show(); 
      }
      if (clockTickCounter == 4) { 
        digitalWrite(ledPin, LOW);         
        pixels.setPixelColor(0, colorTempo); pixels.show(); 
      }
    }
    
    clockTickCounter++;
    if (clockTickCounter >= 24) clockTickCounter = 0;
  }

  int reading = digitalRead(pedalPin);
  if ( (nowMillis - lastDebounceTime) > debounceDelay ) {
    if (reading == HIGH && !isPressed) { 
      lastDebounceTime = nowMillis; 
      isPressed = true;
      pressStartTime = nowMillis;
      longPressHandled = false; 
      
      if (!startMode) { pixels.setPixelColor(0, colorListen); pixels.show(); }

      if (startMode) {
        sendResetSequence();
        clockTickCounter = 0;
        lastClockMicros = nowMicros;
        pixels.setPixelColor(0, pixels.Color(255, 255, 255)); pixels.show(); delay(50);
      } else {
        unsigned long tapGap = nowMillis - lastPressTime;
        if (tapGap > 250 && tapGap < 2500) {
           float variation = 0;
           if (lastValidInterval > 0) variation = abs((long)tapGap - lastValidInterval) / (float)lastValidInterval;
           if (variation > 0.30) newSequence = true; 

           if (newSequence) {
             for(int i=0; i<3; i++) tapIntervals[i] = tapGap;
             newSequence = false;
             tapsInSequence = 1;
             lastValidInterval = tapGap;
             clockTickCounter = 0; 
             lastClockMicros = nowMicros; 
           } else {
             tapsInSequence++; 
             tapIntervals[tapIndex] = tapGap;
             tapIndex = (tapIndex + 1) % 3; 

             if (tapsInSequence >= 3) {
                 long avg = (tapIntervals[0] + tapIntervals[1] + tapIntervals[2]) / 3;
                 if (avg > 0) {
                   currentBPM = round(60000.0 / avg); 
                   if (currentBPM < 40) currentBPM = 40;
                   if (currentBPM > 240) currentBPM = 240;
                   clockIntervalMicros = (60.0 / currentBPM / 24.0) * 1000000.0;
                   lastValidInterval = avg; 
                   
                   if (tapsInSequence == 3) {
                       clockTickCounter = 0;
                       lastClockMicros = nowMicros;
                   }
                 }
             }
           }
        } else if (tapGap > 2500) {
          newSequence = true; 
          tapsInSequence = 0; 
        }
        lastPressTime = nowMillis; 
      }
    }
    if (reading == LOW && isPressed) {
       lastDebounceTime = nowMillis; 
       isPressed = false; 
    }
  }

  if (isPressed && !longPressHandled) {
    if (nowMillis - pressStartTime > 2000) {
      startMode = !startMode; 
      longPressHandled = true;
      if (startMode) {
        pixels.setPixelColor(0, colorStart); digitalWrite(ledPin, HIGH);
      } else {
        pixels.setPixelColor(0, colorTempo); digitalWrite(ledPin, LOW);
      }
      pixels.show();
    }
  }
}
