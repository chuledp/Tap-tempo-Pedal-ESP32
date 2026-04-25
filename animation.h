#include <avr/pgmspace.h>

// Animación 'movieout' a 128x64px
const unsigned char frame_0 [] PROGMEM = { /* ... tus datos HEX aquí ... */ };
const unsigned char frame_1 [] PROGMEM = { /* ... tus datos HEX aquí ... */ };
// ... repetir para los 16 frames ...

const int epd_bitmap_allArray_LEN = 16;
const unsigned char* epd_bitmap_allArray[16] = {
  frame_0, frame_1, frame_2, frame_3, frame_4, frame_5, frame_6, frame_7,
  frame_8, frame_9, frame_10, frame_11, frame_12, frame_13, frame_14, frame_15
};
