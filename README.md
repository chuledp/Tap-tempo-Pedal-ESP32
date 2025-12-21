# ESP32 Smart MIDI Tap Tempo

Un controlador de Tap Tempo MIDI avanzado basado en ESP32, diseñado para sincronizar hardware híbrido (Roland SP-404 mkII y Arturia MicroFreak) con lógica adaptativa y corrección de voltaje.

## 🚀 Características

* **Lógica Adaptativa ("Elastic Tempo"):** Estabiliza pequeñas variaciones humanas pero responde suavemente a cambios intencionales de velocidad (rallentando/accelerando).
* **Dual MIDI Output:** Salidas duplicadas para manejar simultáneamente equipos con diferentes necesidades de cableado o impedancia.
* **Soporte "Normalmente Cerrado" (NC):** Lógica invertida para compatibilidad con pedales de sustain estilo Boss/Roland.
* **Alineación "On-The-Fly":** El reloj se alinea instantáneamente al pisotón, permitiendo corregir el tiempo de la banda en vivo sin cortar la música.
* **Modo Pánico/Reset:** Presión larga (2s) activa un modo para enviar `MIDI START` (0xFA) y reiniciar las secuencias al compás 1.
* **Sincronización Visual Perfecta:** El LED externo y el NeoPixel interno disparan ticks cortos (staccato) para una referencia visual precisa.

## 🛠️ Hardware

* **Microcontrolador:** ESP32-S3 (DevKitC-1 recomendado).
* **Conexión MIDI:** Salida directa Serial a 31250 baudios (3.3V Logic).
* **Pedal:** Pedal de sustain estándar (Jack 6.3mm).
* **Indicadores:** LED RGB integrado (Pin 48) + LED Rojo Externo (Pin 39).

### Pinout (Configurable en código)

| Componente | Pin ESP32 (GPIO) | Notas |
| :--- | :--- | :--- |
| **MIDI TX 1** | GPIO 17 | Salida Principal |
| **MIDI TX 2** | GPIO 43 | Salida Secundaria |
| **Pedal** | GPIO 13 | INPUT_PULLUP (Masa al pisar/soltar) |
| **LED Ext** | GPIO 39 | LED indicador rojo |
| **RGB LED** | GPIO 48 | NeoPixel integrado |

## 🔌 Instalación

1.  Instalar **Arduino IDE**.
2.  Instalar las librerías necesarias:
    * `Adafruit NeoPixel`
3.  Seleccionar la placa `ESP32S3 Dev Module`.
4.  Cargar el archivo `MidiTapTempo.ino`.

## 🎮 Instrucciones de Uso

1.  **Tap Tempo (Modo Azul):**
    * Pisa el pedal al ritmo de la música (Negras).
    * La luz parpadeará siguiendo el BPM calculado.
    * Pisa 3 veces para establecer un nuevo promedio estable.

2.  **Start / Reset (Modo Rojo):**
    * Mantén el pedal presionado **2 segundos**.
    * La luz cambiará a **ROJO FIJO**.
    * Un pisotón enviará la señal `MIDI START` para reiniciar tus secuenciadores al inicio.
    * Mantén presionado 2 segundos para volver al modo Tempo.

---
