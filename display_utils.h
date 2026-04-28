#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Definiciones de pantalla
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

// Instancia global (se vincula con el .ino mediante extern si es necesario)
extern Adafruit_SSD1306 display;

// --- FUNCIONES INTERNAS DE DIBUJO ---

void dibujarMacroIzquierda(int x, int y, String nombre, int valor) {
  display.setCursor(x, y);
  display.print(nombre);
  display.setCursor(x, y + 12); 
  display.print(valor);
}

void dibujarMacroDerecha(int x, int y, String nombre, int valor) {
  // Calculamos posición según longitud de nombre (6px por caracter)
  int xNombre = x - (nombre.length() * 6);
  display.setCursor(xNombre, y);
  display.print(nombre);
  
  // Ajuste de posición del valor según cantidad de dígitos
  int anchoVal = (valor >= 100) ? 18 : (valor >= 10) ? 12 : 6;
  display.setCursor(x - anchoVal, y + 12); 
  display.print(valor);
}

// --- FUNCIÓN PRINCIPAL DE REFRESCO ---
// Esta es la que llamarás desde el loop del .ino

void actualizarOLED(float bpm, bool wifi, int modo, String modos[], String nombres[], int valores[], int clockTick) {
  display.clearDisplay();
  
  // IMPORTANTE: Color con fondo explícito para evitar el glitch de superposición
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); 
  display.setTextSize(1);

  // 1. HEADER (BPM, Círculo, WiFi y Modo)
  display.setCursor(0, 0);
  display.print((int)bpm);
  
  // Dibujar círculo de tempo
  if (clockTick < 6) {
    display.fillCircle(32, 3, 2, SSD1306_WHITE);
  }

  display.setCursor(55, 0);
  display.print(wifi ? "WC" : "WD");

  String mStr = "[" + modos[modo] + "]";
  display.setCursor(128 - (mStr.length() * 6), 0);
  display.print(mStr);

  // 2. LÍNEAS DIVISORIAS
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE); // Horizontal
  display.drawLine(64, 15, 64, 60, SSD1306_WHITE); // Vertical central

  // 3. MACROS (Grilla simétrica)
  // Izquierda (Macros 1 y 3)
  dibujarMacroIzquierda(4, 18, nombres[0], valores[0]); 
  dibujarMacroIzquierda(4, 42, nombres[2], valores[2]);
  
  // Derecha (Macros 2 y 4)
  dibujarMacroDerecha(124, 18, nombres[1], valores[1]); 
  dibujarMacroDerecha(124, 42, nombres[3], valores[3]);

  // 4. ENVIAR AL HARDWARE
  display.display();
}

#endif
