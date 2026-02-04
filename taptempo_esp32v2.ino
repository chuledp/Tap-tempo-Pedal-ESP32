/*
 * ESP32 MIDI TAP TEMPO - FINAL "NO HANGING NOTES"
 * * Híbrido Lock & Glide
 * * INCLUYE: Protocolo de limpieza de notas al reiniciar (Panic)
 */

#include <Arduino.h>
#include <math.h> 
#include <Adafruit_NeoPixel.h> 

// --- PINES ---
const int txPin1 = 17;
const int txPin2 = 43;
const int pedalPin = 13;
const int ledPin = 39; 

// --- CONFIGURACIÓN RGB ---
const int rgbPin = 48; 
const int numPixels = 1;
Adafruit_NeoPixel pixels(numPixels, rgbPin, NEO_GRB + NEO_KHZ800);

// --- COLORES ---
uint32_t colorTempo  = pixels.Color(0, 0, 50);     // Azul Tenue
uint32_t colorFlash  = pixels.Color(50, 50, 255);  // Azul Fuerte
uint32_t colorStart  = pixels.Color(255, 0, 0);    // Rojo
uint32_t colorListen = pixels.Color(180, 0, 255);  // VIOLETA

bool startMode = false; 

// --- VARIABLES RELOJ ---
float currentBPM = 120.0;
unsigned long lastClockMicros = 0;
unsigned long clockIntervalMicros = 20833; 
int clockTickCounter = 0;

// --- VARIABLES PEDAL ---
unsigned long pressStartTime = 0;
bool isPressed = false;
bool longPressHandled = false;
unsigned long lastPressTime = 0; 
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50; 

// --- VARIABLES TAP TEMPO ---
long tapIntervals[3]; 
int tapIndex = 0;
bool newSequence = true; 
int tapsInSequence = 0;  
long lastValidInterval = 500; 

// Timeout Fijo
const unsigned long TIMEOUT_MS = 2000;
bool isListeningMode = false;

// === FUNCIÓN AUXILIAR: PÁNICO Y RESET ===
// Envía Stop + All Notes Off + Start
void sendResetSequence() {
  // 1. STOP (Detiene transporte)
  Serial1.write(0xFC); Serial2.write(0xFC);
  delay(5); // Pequeña pausa para procesar
  
  // 2. ALL NOTES OFF (CC 123) en Canal 1 (Global)
  // Mensaje MIDI: [Control Change Ch1, CC Number 123, Value 0]
  Serial1.write(0xB0); Serial1.write(123); Serial1.write(0);
  Serial2.write(0xB0); Serial2.write(123); Serial2.write(0);
  delay(5);

  // 3. START (Arranca de cero)
  Serial1.write(0xFA); Serial2.write(0xFA);
}

void setup() {
  Serial1.begin(31250, SERIAL_8N1, -1, txPin1);
  Serial2.begin(31250, SERIAL_8N1, -1, txPin2);
  
  pinMode(pedalPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear(); pixels.show();
  
  for(int i=0; i<3; i++) tapIntervals[i] = 500; 
  
  // Arrancamos limpio
  sendResetSequence();
}

void loop() {
  unsigned long nowMicros = micros();
  unsigned long nowMillis = millis();

  // CHEQUEO TIMEOUT
  if (tapsInSequence > 0 && (nowMillis - lastPressTime < TIMEOUT_MS)) {
    isListeningMode = true;
  } else {
    isListeningMode = false;
    if (tapsInSequence > 0) {
      tapsInSequence = 0;
      newSequence = true;
    }
  }

  // 1. RELOJ MIDI
  if (nowMicros - lastClockMicros >= clockIntervalMicros) {
    lastClockMicros += clockIntervalMicros;
    Serial1.write(0xF8); Serial2.write(0xF8);
    
    // VISUAL
    if (startMode) {
      digitalWrite(ledPin, HIGH); 
      pixels.setPixelColor(0, colorStart); pixels.show();
    } else if (isListeningMode) {
       digitalWrite(ledPin, LOW); 
       pixels.setPixelColor(0, colorListen); 
       pixels.show();
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

  // 2. PEDAL INVERTIDO (NC)
  int reading = digitalRead(pedalPin);

  if ( (nowMillis - lastDebounceTime) > debounceDelay ) {

    // AL PISAR
    if (reading == HIGH && !isPressed) { 
      
      lastDebounceTime = nowMillis; 
      isPressed = true;
      pressStartTime = nowMillis;
      longPressHandled = false; 
      
      // Feedback inmediato
      if (!startMode) {
         pixels.setPixelColor(0, colorListen); pixels.show();
      }

      if (startMode) {
        // >>> AQUÍ USAMOS LA NUEVA FUNCIÓN <<<
        // En lugar de solo FA, mandamos la secuencia de limpieza
        sendResetSequence();
        
        clockTickCounter = 0;
        lastClockMicros = nowMicros;
        pixels.setPixelColor(0, pixels.Color(255, 255, 255)); pixels.show(); delay(50);
        
      } else {
        // --- TAP TEMPO ---
        unsigned long tapGap = nowMillis - lastPressTime;
        
        if (tapGap > 250 && tapGap < 2500) {
           
           // DETECTOR DE CAMBIO BRUSCO
           float variation = 0;
           if (lastValidInterval > 0) {
              variation = abs((long)tapGap - lastValidInterval) / (float)lastValidInterval;
           }
           if (variation > 0.30) newSequence = true; 

           if (newSequence) {
             // TAP 1
             for(int i=0; i<3; i++) tapIntervals[i] = tapGap;
             newSequence = false;
             tapsInSequence = 1;
             lastValidInterval = tapGap;
             
             // HARD SYNC
             clockTickCounter = 0; 
             lastClockMicros = nowMicros; 
             
           } else {
             tapsInSequence++; 
             tapIntervals[tapIndex] = tapGap;
             tapIndex = (tapIndex + 1) % 3; 

             // TAP 3+
             if (tapsInSequence >= 3) {
                 long avg = (tapIntervals[0] + tapIntervals[1] + tapIntervals[2]) / 3;
                 
                 if (avg > 0) {
                   currentBPM = round(60000.0 / avg); 
                   if (currentBPM < 40) currentBPM = 40;
                   if (currentBPM > 240) currentBPM = 240;
                   
                   clockIntervalMicros = (60.0 / currentBPM / 24.0) * 1000000.0;
                   lastValidInterval = avg; 
                   
                   // Híbrido Sync (Solo en Tap 3)
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

    // AL SOLTAR
    if (reading == LOW && isPressed) {
       lastDebounceTime = nowMillis; 
       isPressed = false; 
    }
  }

  // CHEQUEO MODO START
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