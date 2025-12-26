#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "opl.h"

// F-Number table for Octave 4 @ 4.0 MHz
const uint16_t fnum_table[12] = {
    308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579
};

// Shadow registers for all 9 channels
// We need this to remember the Block/F-Number when we send a NoteOff
uint8_t shadow_b0[9] = {0}; 

// Global state for the music sequencer
uint16_t current_event_idx = 0;
uint16_t ticks_until_next_event = 0;

// Returns a 16-bit value: 
// High Byte: 0x20 (KeyOn) | Block << 2 | F-Number High (2 bits)
// Low Byte: F-Number Low (8 bits)
uint16_t midi_to_opl_freq(uint8_t midi_note) {
    // MIDI 60 is C-4.
    int8_t octave = (midi_note / 12) - 1; 
    uint8_t note_idx = midi_note % 12;

    if (octave < 0) octave = 0;
    if (octave > 7) octave = 7;

    uint16_t fnum = fnum_table[note_idx];
    
    uint8_t high = 0x20 | (octave << 2) | ((fnum >> 8) & 0x03);
    uint8_t low = fnum & 0xFF;

    return (high << 8) | low;
}

void opl_write(uint8_t reg, uint8_t data) {
    RIA.addr0 = 0xFF00;
    RIA.step0 = 1;
    RIA.rw0 = reg;
    RIA.rw0 = data;
}

void OPL_NoteOn(uint8_t channel, uint8_t midi_note) {
    if (channel > 8) return;
    uint16_t freq = midi_to_opl_freq(midi_note);
    
    opl_write(0xA0 + channel, freq & 0xFF);         // F-Number Low
    opl_write(0xB0 + channel, (freq >> 8) & 0xFF);  // KeyOn + Block + F-High
    shadow_b0[channel] = (freq >> 8) & 0x1F;        // Store without KeyOn (bit 5)
}

void OPL_NoteOff(uint8_t channel) {
    if (channel > 8) return;
    opl_write(0xB0 + channel, shadow_b0[channel]); // Write stored octave/freq with KeyOn=0
}

void opl_clear() {
    for (int i = 0; i < 256; i++) {
        opl_write(false, i);
        opl_write(true,  0x00);
    }
    // Clear shadow memory too
    for (int i=0; i<9; i++) shadow_b0[i] = 0;
}
