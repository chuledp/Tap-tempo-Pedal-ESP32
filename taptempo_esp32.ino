/*
 * ESP32 MIDI TAP TEMPO - DUAL OUTPUT
 * * Proyecto: Sincronización para Roland SP-404 mkII y Arturia MicroFreak
 * Hardware: ESP32-S3 (DevKitC-1)
 * Autor: Julián Di Pietro
 * Fecha: Diciembre 2025
 * * DESCRIPCIÓN:
 * Controlador de Tap Tempo MIDI con lógica inteligente.
 * - Soporta pedales "Normalmente Cerrados" (NC).
 * - Algoritmo de suavizado adaptativo ("Elastic Tempo").
 * - Doble salida Serial para cubrir Jacks TRS Tipo A y Tipo B.
 * - Feedback visual sincronizado (LED Externo + RGB interno).
 * - Modo "Pánico/Start" mediante presión larga.
 * * DEPENDENCIAS:
 * - Librería Adafruit NeoPixel
 */

#include <Arduino.h>
#include <math.h> 
#include <Adafruit_NeoPixel.h> 

// --- PINES ---
// Salidas MIDI (3.3V Logic)
const int txPin1 = 17;     // Salida Principal
const int txPin2 = 43;     // Salida Secundaria (Backup/Dual)
// Entradas y Salidas Físicas
const int pedalPin = 13;   // Pedal de Sustain (Configurado para NC)
const int ledPin = 20;     // LED Rojo Externo

// --- CONFIGURACIÓN RGB (ESP32-S3 Built-in) ---
const int rgbPin = 48; 
const int numPixels = 1;
Adafruit_NeoPixel pixels(numPixels, rgbPin, NEO_GRB + NEO_KHZ800);

// --- PALETA DE COLORES ---
uint32_t colorTempo = pixels.Color(0, 0, 50);    // Azul Tenue (Reposo)
uint32_t colorFlash = pixels.Color(50, 50, 255); // Azul Fuerte (Beat)
uint32_t colorStart = pixels.Color(255, 0, 0);   // Rojo (Modo Start)

// --- ESTADOS ---
bool startMode = false; // false = Tap Tempo, true = Modo Start/Reset

// --- VARIABLES DE RELOJ MIDI ---
float currentBPM = 120.0;
unsigned long lastClockMicros = 0;
unsigned long clockIntervalMicros = 20833; // 120 BPM por defecto
int clockTickCounter = 0;

// --- VARIABLES DEL PEDAL (LÓGICA NC) ---
unsigned long pressStartTime = 0;
bool isPressed = false;
bool longPressHandled = false;
unsigned long lastPressTime = 0; 
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50; // Filtro anti-rebote (50ms)

// --- VARIABLES DE PROMEDIO (TAP TEMPO) ---
long tapIntervals[3]; 
int tapIndex = 0;
bool newSequence = true; // Indica si empezamos una nueva serie de taps

// ================================================================
// SETUP
// ================================================================
void setup() {
  // Inicialización de puertos Seriales para MIDI (Baud 31250)
  Serial1.begin(31250, SERIAL_8N1, -1, txPin1);
  Serial2.begin(31250, SERIAL_8N1, -1, txPin2);
  
  // Configuración de Pines
  pinMode(pedalPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  // Inicialización LED RGB
  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear(); 
  pixels.show();
  
  // Inicializar array de intervalos (Default 120 BPM = 500ms)
  for(int i=0; i<3; i++) tapIntervals[i] = 500; 
  
  // Enviar mensaje de START al iniciar
  Serial1.write(0xFA); Serial2.write(0xFA);
}

// ================================================================
// LOOP PRINCIPAL
// ================================================================
void loop() {
  unsigned long nowMicros = micros();
  unsigned long nowMillis = millis();

  // --------------------------------------------------------------
  // 1. GENERADOR DE RELOJ MIDI
  // --------------------------------------------------------------
  if (nowMicros - lastClockMicros >= clockIntervalMicros) {
    lastClockMicros += clockIntervalMicros;
    
    // Enviar Tick MIDI (0xF8) a ambas salidas
    Serial1.write(0xF8); Serial2.write(0xF8);
    
    // --- LÓGICA VISUAL ---
    if (startMode) {
      // MODO START: LED Fijo Rojo
      digitalWrite(ledPin, HIGH); 
      pixels.setPixelColor(0, colorStart); 
      pixels.show();
    } else {
      // MODO TEMPO: Flash sincronizado (Duración corta para precisión)
      
      // ENCENDIDO (Tick 0 - Downbeat)
      if (clockTickCounter == 0) { 
        digitalWrite(ledPin, HIGH);        
        pixels.setPixelColor(0, colorFlash); 
        pixels.show(); 
      }
      
      // APAGADO RÁPIDO (Tick 4) - Corte tipo Staccato
      if (clockTickCounter == 4) { 
        digitalWrite(ledPin, LOW);         
        pixels.setPixelColor(0, colorTempo); 
        pixels.show(); 
      }
    }
    
    clockTickCounter++;
    if (clockTickCounter >= 24) clockTickCounter = 0;
  }

  // --------------------------------------------------------------
  // 2. LECTURA DEL PEDAL (Lógica Invertida - Normalmente Cerrado)
  // --------------------------------------------------------------
  int reading = digitalRead(pedalPin);

  // Filtro de Rebote (Debounce)
  if ( (nowMillis - lastDebounceTime) > debounceDelay ) {

    // --- AL PISAR --- 
    // Nota: En pedales NC, pisar abre el circuito, resultando en HIGH (INPUT_PULLUP)
    if (reading == HIGH && !isPressed) { 
      
      lastDebounceTime = nowMillis; 
      isPressed = true;
      pressStartTime = nowMillis;
      longPressHandled = false; 
      
      if (startMode) {
        // ACCIÓN: START / RESET
        // Envía 0xFA para reiniciar las secuencias al compás 1
        Serial1.write(0xFA); Serial2.write(0xFA);
        
        // Reseteo visual
        clockTickCounter = 0;
        lastClockMicros = nowMicros;
        pixels.setPixelColor(0, pixels.Color(255, 255, 255)); // Flash Blanco
        pixels.show(); 
        delay(50);
        
      } else {
        // ACCIÓN: TAP TEMPO
        unsigned long tapGap = nowMillis - lastPressTime;
        
        // Filtro de rango humano (40 BPM a 240 BPM aprox)
        if (tapGap > 250 && tapGap < 2000) {
           
           if (newSequence) {
             // Si es el primer tap de una serie, reiniciamos el promedio
             for(int i=0; i<3; i++) tapIntervals[i] = tapGap;
             newSequence = false;
           } else {
             // Promedio Circular de los últimos 3 taps
             tapIntervals[tapIndex] = tapGap;
             tapIndex = (tapIndex + 1) % 3; 
           }

           long avg = (tapIntervals[0] + tapIntervals[1] + tapIntervals[2]) / 3;
           
           if (avg > 0) {
             // Cálculo de BPM (1 Tap = 1 Negra)
             currentBPM = round(60000.0 / avg); 
             
             // Límites de seguridad (Clamping)
             if (currentBPM < 40) currentBPM = 40;
             if (currentBPM > 240) currentBPM = 240;
             
             // Actualizar intervalo del reloj
             clockIntervalMicros = (60.0 / currentBPM / 24.0) * 1000000.0;
             
             // Alineación Inmediata del Beat ("On The Fly")
             // Sincroniza el "Uno" con el pisotón
             clockTickCounter = 0;
             lastClockMicros = nowMicros; 
           }
        } else if (tapGap > 2000) {
          // Si pasó mucho tiempo, la próxima pisada inicia una nueva secuencia
          newSequence = true; 
        }
        lastPressTime = nowMillis; 
      }
    }

    // --- AL SOLTAR ---
    // Nota: En pedales NC, soltar cierra el circuito a Tierra (LOW)
    if (reading == LOW && isPressed) {
       lastDebounceTime = nowMillis; 
       isPressed = false; 
    }
  }

  // --------------------------------------------------------------
  // 3. GESTIÓN DE MODOS (PRESIÓN LARGA)
  // --------------------------------------------------------------
  if (isPressed && !longPressHandled) {
    // Si se mantiene pisado más de 2 segundos
    if (nowMillis - pressStartTime > 2000) {
      startMode = !startMode; // Cambiar modo
      longPressHandled = true; // Evitar rebotes
      
      // Feedback Visual del Cambio
      if (startMode) {
        pixels.setPixelColor(0, colorStart); digitalWrite(ledPin, HIGH);
      } else {
        pixels.setPixelColor(0, colorTempo); digitalWrite(ledPin, LOW);
      }
      pixels.show();
    }
  }
}
