/*
 * CEREBRO MIDI ESP32-S3
 * V4.8 - HTML mobile + macros 9 chars uppercase
 * V4.0 - Tap Tempo + Display SH1106 + Macros + WebServer
 *
 * Hardware: ESP32-S3 (DevKitC-1)
 * Display:  SH1106G 128x64 I2C (pines 8/9)
 * Pedal:    Normalmente Cerrado (NC) - HIGH = pisado, LOW = suelto
 *
 * LED AZUL   → METRO: sigue el beat (modo por defecto)
 * LED VIOLETA → TAP: mientras se tapea y hasta 2s después del último tap
 * LED VERDE  → SEQ: modo activo; cada tap manda STOP+AllNotesOff+START
 * Hold 3-6s  → toggle SEQ/METRO
 * Hold >6s   → toggle WiFi ON/OFF
 *
 * TAP TEMPO:
 *   Primer tap: hard sync, no cambia BPM todavía
 *   Desde el 3er tap: promedia los últimos 3 intervalos
 *   Cambio brusco >30% → reinicia la secuencia
 *   Timeout 2s sin tap → vuelve a METRO, BPM conservado
 */

#include <Arduino.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WebServer.h>

// ─────────────────────────────────────────────
// HARDWARE
// ─────────────────────────────────────────────
const int txPin    = 43;
const int pedalPin = 13;
const int ledPin   = 39;
const int rgbPin   = 48;
const int potPins[4] = {4, 5, 6, 7};

// ─────────────────────────────────────────────
// DISPLAY SH1106G
// ─────────────────────────────────────────────
#define SCREEN_W  128
#define SCREEN_H   64
#define OLED_ADDR 0x3C
Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, -1);

// ─────────────────────────────────────────────
// RGB NEOPIXEL
// ─────────────────────────────────────────────
Adafruit_NeoPixel pixels(1, rgbPin, NEO_GRB + NEO_KHZ800);

uint32_t COL_METRO;
uint32_t COL_FLASH;
uint32_t COL_TAP;
uint32_t COL_SEQ;
uint32_t COL_RESET;
uint32_t COL_HOLD;

// ─────────────────────────────────────────────
// WIFI + WEBSERVER
// ─────────────────────────────────────────────
const char* ssid     = "MACRONET";
const char* password = "admin1234";
WebServer server(80);
bool wifiActivo = true;

// ─────────────────────────────────────────────
// MODOS
// ─────────────────────────────────────────────
bool seqMode         = false;
bool isListeningMode = false;

// ─────────────────────────────────────────────
// RELOJ MIDI
// ─────────────────────────────────────────────
float         currentBPM          = 80.0;
unsigned long lastClockMicros     = 0;
unsigned long clockIntervalMicros = (unsigned long)(60.0 / 80.0 / 24.0 * 1000000.0);
int           clockTickCounter    = 0;
bool          ledState            = false;

// ─────────────────────────────────────────────
// PEDAL (NA: LOW=pisado, HIGH=suelto)
// ─────────────────────────────────────────────
bool          isPressed        = false;
bool          longPressHandled = false;
unsigned long pressStartTime   = 0;
unsigned long lastPressTime    = 0;
unsigned long lastDebounceTime = 0;
const int     debounceDelay    = 80;

// ─────────────────────────────────────────────
// TAP TEMPO
// ─────────────────────────────────────────────
long tapIntervals[3]   = {750, 750, 750};
int  tapIndex          = 0;
bool newSequence       = true;
int  tapsInSequence    = 0;
long lastValidInterval = 750;
const unsigned long TIMEOUT_MS = 2000;

// ─────────────────────────────────────────────
// MACROS
// ─────────────────────────────────────────────
String nombreMacro[4] = {"CUTF", "RESO", "LFOR", "ENVT"};
int    valorMacro[4]  = {0, 0, 0, 0};
int    lastRawPot[4]  = {-1, -1, -1, -1};
int    lastSentCC[4]  = {-1, -1, -1, -1};
const int ccMacro[4]  = {23, 83, 93, 105};

// ─────────────────────────────────────────────
// DISPLAY TIMING
// ─────────────────────────────────────────────
unsigned long lastDisplayMillis = 0;
const int     DISPLAY_INTERVAL  = 80;

// ─────────────────────────────────────────────
// BOOT: ignorar pedal al arrancar (pin tarda en estabilizar)
// ─────────────────────────────────────────────
unsigned long bootIgnoreUntil = 0;
const int     BOOT_IGNORE_MS  = 500;

// ─────────────────────────────────────────────
// HTML
// ─────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0">
<title>CEREBRO MIDI</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0a0a0a;--panel:#141414;--card:#1a1a1a;--border:#2a2a2a;--accent:#f0a500;--text:#c8c8c8;--dim:#666;--green:#48bb78}
body{font-family:'Courier New',monospace;background:var(--bg);color:var(--text);padding:12px;min-height:100vh}
header{text-align:center;padding:16px 0 20px;border-bottom:1px solid var(--border);margin-bottom:16px}
header h1{color:var(--accent);font-size:20px;letter-spacing:4px}
header p{font-size:11px;color:var(--dim);margin-top:4px;letter-spacing:1px}
.macros{display:flex;flex-direction:column;gap:16px}
.card{background:var(--card);border:1px solid var(--border);border-top:3px solid var(--accent);border-radius:6px;overflow:hidden}
.card-header{display:flex;justify-content:space-between;align-items:center;padding:12px 14px;border-bottom:1px solid var(--border)}
.card-header h3{font-size:11px;color:var(--dim);letter-spacing:2px}
input[type=text]{background:var(--bg);color:var(--accent);border:1px solid #333;border-radius:4px;padding:8px 10px;font-family:'Courier New',monospace;font-size:16px;font-weight:bold;text-transform:uppercase;text-align:center;width:160px;letter-spacing:2px}
.save-btn{display:block;width:100%;background:var(--accent);color:#000;border:none;border-radius:6px;padding:16px;font-family:'Courier New',monospace;font-size:15px;font-weight:bold;letter-spacing:2px;cursor:pointer;margin-top:20px}
.toast{display:none;text-align:center;color:var(--green);font-size:12px;letter-spacing:1px;margin-top:10px;padding:8px}
</style>
</head>
<body>
<header>
<h1>CEREBRO MIDI</h1>
<p>MACRONET</p>
</header>
<div class="macros">
<div class="card"><div class="card-header"><h3>MACRO 1</h3><input type="text" id="m1" maxlength="9" placeholder="CUTF" oninput="this.value=this.value.toUpperCase()"></div></div>
<div class="card"><div class="card-header"><h3>MACRO 2</h3><input type="text" id="m2" maxlength="9" placeholder="RESO" oninput="this.value=this.value.toUpperCase()"></div></div>
<div class="card"><div class="card-header"><h3>MACRO 3</h3><input type="text" id="m3" maxlength="9" placeholder="LFOR" oninput="this.value=this.value.toUpperCase()"></div></div>
<div class="card"><div class="card-header"><h3>MACRO 4</h3><input type="text" id="m4" maxlength="9" placeholder="ENVT" oninput="this.value=this.value.toUpperCase()"></div></div>
</div>
<button class="save-btn" onclick="guardar()">GUARDAR EN ESP32</button>
<div class="toast" id="toast">GUARDADO</div>
<script>
function guardar(){
  var p=new URLSearchParams();
  p.append('n1',document.getElementById('m1').value.toUpperCase()||'CUTF');
  p.append('n2',document.getElementById('m2').value.toUpperCase()||'RESO');
  p.append('n3',document.getElementById('m3').value.toUpperCase()||'LFOR');
  p.append('n4',document.getElementById('m4').value.toUpperCase()||'ENVT');
  fetch('/update?'+p.toString()).then(function(){
    var t=document.getElementById('toast');
    t.style.display='block';
    setTimeout(function(){t.style.display='none'},2000);
  });
}
</script>
</body>
</html>
)=====";

// ─────────────────────────────────────────────
// DISPLAY
// ─────────────────────────────────────────────

void dibujarMacroIzq(int x, int y, String nombre, int valor) {
  if (nombre.length() > 9) nombre = nombre.substring(0, 9);
  display.setCursor(x, y);
  display.print(nombre);
  display.setCursor(x, y + 12);
  display.print(valor);
}

void dibujarMacroDer(int x, int y, String nombre, int valor) {
  if (nombre.length() > 9) nombre = nombre.substring(0, 9);
  int xN = x - (int)(nombre.length() * 6);
  display.setCursor(xN, y);
  display.print(nombre);
  int ancho = (valor >= 100) ? 18 : (valor >= 10) ? 12 : 6;
  display.setCursor(x - ancho, y + 12);
  display.print(valor);
}

void actualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);
  display.print((int)currentBPM);

  if (clockTickCounter < 6) {
    display.fillCircle(32, 3, 2, SH110X_WHITE);
  }

  display.setCursor(55, 0);
  display.print(wifiActivo ? "WC" : "WD");

  const char* modoStr;
  if      (seqMode)         modoStr = "SEQ";
  else if (isListeningMode) modoStr = "TAP";
  else                      modoStr = "METRO";

  String mStr = "[";
  mStr += modoStr;
  mStr += "]";
  display.setCursor(128 - (int)(mStr.length() * 6), 0);
  display.print(mStr);

  display.drawLine(0,  10, 128, 10, SH110X_WHITE);
  display.drawLine(64, 15,  64, 60, SH110X_WHITE);

  dibujarMacroIzq( 4, 18, nombreMacro[0], valorMacro[0]);
  dibujarMacroIzq( 4, 42, nombreMacro[2], valorMacro[2]);
  dibujarMacroDer(124, 18, nombreMacro[1], valorMacro[1]);
  dibujarMacroDer(124, 42, nombreMacro[3], valorMacro[3]);

  display.display();
}

// Dibuja las 5 líneas con dithering en el buffer (sin display.display())
void splashFrame() {
  const int dith[5] = {3, 2, 0, 2, 3};
  const int x = (SCREEN_W - 11 * 6) / 2;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  for (int i = 0; i < 5; i++) {
    display.setCursor(x, 8 + i * 8);
    display.print("RECOLECTORA");
  }
  uint8_t* buf = display.getBuffer();
  for (int i = 0; i < 5; i++) {
    if (dith[i] == 0) continue;
    int off = ((8 + i * 8) / 8) * SCREEN_W;
    for (int col = 0; col < SCREEN_W; col++) {
      if (col % dith[i] != 0) buf[off + col] = 0;
    }
  }
}

void mostrarSplash() {
  unsigned long t;
  uint8_t* buf   = display.getBuffer();
  int      bufSz = SCREEN_W * (SCREEN_H / 8);

  // ── Fase 1: ruido puro (600ms) ──────────────
  t = millis();
  while (millis() - t < 600) {
    for (int b = 0; b < bufSz; b++) buf[b] = random(256);
    display.display();
    delay(16);
  }

  // ── Fase 2: texto emerge del ruido (1800ms) ──
  t = millis();
  while (millis() - t < 1800) {
    int p = (int)((millis() - t) * 100 / 1800); // 0→100
    splashFrame();
    for (int b = 0; b < bufSz; b++) {
      if (random(100) < (100 - p) * 55 / 100) buf[b] ^= random(256);
    }
    display.display();
    delay(30);
  }

  // ── Fase 3: display limpio (1200ms) ──────────
  splashFrame();
  display.display();
  delay(1200);

  // ── Fase 4: glitch bursts (1500ms) ───────────
  t = millis();
  while (millis() - t < 1500) {
    splashFrame();
    if (random(3) == 0) {
      int gc = random(SCREEN_W - 20);
      int gw = random(2, 18);
      for (int c = gc; c < gc + gw; c++) {
        for (int pg = 0; pg < SCREEN_H / 8; pg++) {
          buf[pg * SCREEN_W + c] ^= 0xFF;
        }
      }
    }
    for (int b = 0; b < bufSz; b++) {
      if (random(100) < 4) buf[b] ^= random(256);
    }
    display.display();
    delay(45);
  }

  display.clearDisplay();
  display.display();
}

// ─────────────────────────────────────────────
// MIDI
// ─────────────────────────────────────────────

void sendResetSequence() {
  Serial1.write(0xFC);
  delay(5);
  Serial1.write(0xB0); Serial1.write(123); Serial1.write(0);
  delay(5);
  Serial1.write(0xFA);
}

// ─────────────────────────────────────────────
// WEB HANDLERS
// ─────────────────────────────────────────────

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleUpdate() {
  if (server.hasArg("n1")) { nombreMacro[0] = server.arg("n1"); nombreMacro[0].toUpperCase(); }
  if (server.hasArg("n2")) { nombreMacro[1] = server.arg("n2"); nombreMacro[1].toUpperCase(); }
  if (server.hasArg("n3")) { nombreMacro[2] = server.arg("n3"); nombreMacro[2].toUpperCase(); }
  if (server.hasArg("n4")) { nombreMacro[3] = server.arg("n4"); nombreMacro[3].toUpperCase(); }
  server.send(200, "text/plain", "OK");
}

// ─────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial1.begin(31250, SERIAL_8N1, -1, txPin);
  randomSeed(micros());

  pinMode(pedalPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  pixels.begin();
  pixels.setBrightness(50);
  COL_METRO = pixels.Color(0,   0,   50);
  COL_FLASH = pixels.Color(50,  50,  255);
  COL_TAP   = pixels.Color(120, 0,   180);
  COL_SEQ   = pixels.Color(0,   180, 0);
  COL_RESET = pixels.Color(200, 180, 0);
  COL_HOLD  = pixels.Color(200, 0,   0);
  pixels.setPixelColor(0, COL_METRO);
  pixels.show();

  Wire.begin(8, 9);
  Wire.setClock(400000);
  display.begin(OLED_ADDR, false);
  display.clearDisplay();
  display.display();

  mostrarSplash();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  server.on("/",       handleRoot);
  server.on("/update", handleUpdate);
  server.begin();

  Serial1.write(0xFA);

  bootIgnoreUntil  = millis() + BOOT_IGNORE_MS;
  lastDebounceTime = millis();
  lastPressTime    = millis();
}

// ─────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────

void loop() {
  unsigned long nowMicros = micros();
  unsigned long nowMillis = millis();

  // ── 0. TIMEOUT TAP ──────────────────────────
  if (tapsInSequence > 0 && (nowMillis - lastPressTime < TIMEOUT_MS)) {
    isListeningMode = true;
  } else {
    if (isListeningMode) {
      isListeningMode = false;
      tapsInSequence  = 0;
      newSequence     = true;
    }
  }

  // ── 1. WEBSERVER ────────────────────────────
  if (wifiActivo) server.handleClient();

  // ── 2. RELOJ MIDI ───────────────────────────
  if (nowMicros - lastClockMicros >= clockIntervalMicros) {
    lastClockMicros += clockIntervalMicros;
    Serial1.write(0xF8);

    clockTickCounter++;
    if (clockTickCounter >= 24) clockTickCounter = 0;

    if (!isPressed) {
      if (clockTickCounter == 0) {
        pixels.setPixelColor(0, COL_FLASH);
        digitalWrite(ledPin, HIGH);
        pixels.show();
        ledState = true;
      } else if (clockTickCounter == 4 && ledState) {
        if      (seqMode)         pixels.setPixelColor(0, COL_SEQ);
        else if (isListeningMode) pixels.setPixelColor(0, COL_TAP);
        else                      pixels.setPixelColor(0, COL_METRO);
        digitalWrite(ledPin, LOW);
        pixels.show();
        ledState = false;
      }
    }
  }

  // ── 3. PEDAL ────────────────────────────────
  int reading = digitalRead(pedalPin);

  if (nowMillis > bootIgnoreUntil && nowMillis - lastDebounceTime > debounceDelay) {

    // AL PISAR (NC: HIGH = pisado)
    if (reading == HIGH && !isPressed) {
      lastDebounceTime = nowMillis;
      isPressed        = true;
      pressStartTime   = nowMillis;
      longPressHandled = false;

      if (seqMode) {
        pixels.setPixelColor(0, COL_RESET);
        pixels.show();
        sendResetSequence();
        clockTickCounter = 0;
        lastClockMicros  = nowMicros;
        delay(30);

      } else {
        pixels.setPixelColor(0, COL_TAP);
        pixels.show();
        // TAP TEMPO: promedio de últimos 3 taps
        unsigned long tapGap = nowMillis - lastPressTime;

        if (tapGap > 250 && tapGap < 2500) {
          // Resetear si el intervalo cambió más del 30%
          float variation = 0;
          if (lastValidInterval > 0)
            variation = abs((long)tapGap - lastValidInterval) / (float)lastValidInterval;
          if (variation > 0.30) newSequence = true;

          if (newSequence) {
            // Primer tap de secuencia: hard sync, sin cambiar BPM
            for (int i = 0; i < 3; i++) tapIntervals[i] = tapGap;
            newSequence       = false;
            tapsInSequence    = 1;
            lastValidInterval = tapGap;
            clockTickCounter  = 0;
            lastClockMicros   = nowMicros;

          } else {
            tapsInSequence++;
            tapIntervals[tapIndex] = tapGap;
            tapIndex = (tapIndex + 1) % 3;

            // Aplica BPM desde el 3er tap en adelante
            if (tapsInSequence >= 3) {
              long avg = (tapIntervals[0] + tapIntervals[1] + tapIntervals[2]) / 3;
              if (avg > 0) {
                currentBPM = round(60000.0 / avg);
                currentBPM = constrain(currentBPM, 40.0, 240.0);
                clockIntervalMicros = (unsigned long)(60.0 / currentBPM / 24.0 * 1000000.0);
                lastValidInterval   = avg;
                if (tapsInSequence == 3) {
                  clockTickCounter = 0;
                  lastClockMicros  = nowMicros;
                }
              }
            }
          }
        } else if (tapGap > 2500) {
          newSequence    = true;
          tapsInSequence = 0;
        }
        lastPressTime = nowMillis;
      }
    }

    // AL SOLTAR (NC: LOW = suelto)
    if (reading == LOW && isPressed) {
      lastDebounceTime  = nowMillis;
      isPressed         = false;
      lastDisplayMillis = 0;

      if (nowMillis - pressStartTime > 6000) {
        wifiActivo = !wifiActivo;
        if (wifiActivo) {
          WiFi.mode(WIFI_AP);
          WiFi.softAP(ssid, password);
          server.begin();
        } else {
          server.stop();
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_OFF);
        }
      }
    }
  }

  // ── 4. HOLD PROGRESIVO ──────────────────────
  if (isPressed && !longPressHandled) {
    unsigned long held = nowMillis - pressStartTime;

    if (held > 3000 && held < 6000) {
      longPressHandled = true;
      seqMode = !seqMode;
      if (seqMode) {
        sendResetSequence();
      } else {
        newSequence    = true;
        tapIndex       = 0;
        tapsInSequence = 0;
      }
    }

    if      (held > 6000) pixels.setPixelColor(0, COL_HOLD);
    else if (held > 3000) pixels.setPixelColor(0, COL_SEQ);
    else                  pixels.setPixelColor(0, seqMode ? COL_RESET : COL_TAP);
    pixels.show();
  }

  // ── 5. POTES → CC MIDI ──────────────────────
  // Desactivado hasta conectar los potes físicamente.
  /*
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(potPins[i]);
    if (abs(raw - lastRawPot[i]) < 15) continue;
    lastRawPot[i] = raw;
    int val = map(raw, 0, 4095, 0, 127);
    valorMacro[i] = val;
    if (val != lastSentCC[i]) {
      Serial1.write(0xB0);
      Serial1.write((uint8_t)ccMacro[i]);
      Serial1.write((uint8_t)val);
      lastSentCC[i] = val;
    }
  }
  */

  // ── 6. DISPLAY ──────────────────────────────
  if (nowMillis - lastDisplayMillis >= DISPLAY_INTERVAL) {
    lastDisplayMillis = nowMillis;
    actualizarDisplay();
  }
}
