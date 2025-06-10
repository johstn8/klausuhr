#ifndef FONT7SEG_H
#define FONT7SEG_H

#include <stdint.h>
#include <pgmspace.h>

// 3x5 font bitmaps for digits 0-9 (bit2=left, bit0=right)
static const uint8_t FONT_3x5[10][5] PROGMEM = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b001,0b001,0b001,0b001,0b001}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b001,0b001,0b001}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}  // 9
};

#endif // FONT7SEG_H
