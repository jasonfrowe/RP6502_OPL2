#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "opl.h"
#include "instruments.h"

// F-Number table for Octave 4 @ 4.0 MHz
const uint16_t fnum_table[12] = {
    308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579
};

// Shadow registers for all 9 channels
// We need this to remember the Block/F-Number when we send a NoteOff
uint8_t shadow_b0[9] = {0}; 

// Track the KSL bits so we don't overwrite them when changing volume
uint8_t shadow_ksl_m[9];
uint8_t shadow_ksl_c[9];

// Returns a 16-bit value: 
// High Byte: 0x20 (KeyOn) | Block << 2 | F-Number High (2 bits)
// Low Byte: F-Number Low (8 bits)
uint16_t midi_to_opl_freq(uint8_t midi_note) {
    if (midi_note < 12) midi_note = 12;
    
    int block = (midi_note - 12) / 12;
    int note_idx = (midi_note - 12) % 12;
    
    if (block > 7) block = 7;

    uint16_t f_num = fnum_table[note_idx];
    uint8_t high_byte = 0x20 | (block << 2) | ((f_num >> 8) & 0x03);
    uint8_t low_byte = f_num & 0xFF;

    return (high_byte << 8) | low_byte;
}

void opl_write(uint8_t reg, uint8_t data) {
    RIA.addr0 = 0xFF00;
    RIA.step0 = 1;
    
    RIA.rw0 = reg;   // Write Index
    
    // Short delay: approx 4 microseconds at 8MHz
    __asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
    __asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");

    RIA.rw0 = data;  // Write Data
    
    // Longer delay: approx 24 microseconds
    // This ensures the OPL2 is ready for the NEXT command
    for(uint8_t i=0; i<32; i++) { __asm__("nop"); }
}

void OPL_NoteOn(uint8_t channel, uint8_t midi_note) {
    if (channel == 8) {
        // Simple Percussion Mapping
        if (midi_note == 35 || midi_note == 36) {
            OPL_SetPatch(8, &drum_bd);
        } else if (midi_note == 38 || midi_note == 40) {
            OPL_SetPatch(8, &drum_snare);
            channel = 7; // Snare uses channel 7 operator
        } else {
            OPL_SetPatch(8, &drum_hihat);
            channel = 6; // Hi-Hat uses channel 6 operator
        }
        // Drums usually play at a fixed high pitch in OPL2 
        // regardless of the MIDI note
        midi_note = 60; 
    }
    
    uint16_t freq = midi_to_opl_freq(midi_note);
    opl_write(0xA0 + channel, freq & 0xFF);
    opl_write(0xB0 + channel, (freq >> 8) & 0xFF);
    shadow_b0[channel] = (freq >> 8) & 0x1F;
}

void OPL_NoteOff(uint8_t channel) {
    if (channel > 8) return;
    opl_write(0xB0 + channel, shadow_b0[channel]); // Write stored octave/freq with KeyOn=0
}

// Clear all 256 registers correctly
void opl_clear() {
    for (int i = 0; i < 256; i++) {
        opl_write(i, 0x00);
    }
    // Reset shadow memory
    for (int i=0; i<9; i++) shadow_b0[i] = 0;
}

void OPL_SetVolume(uint8_t chan, uint8_t velocity) {
    // Convert MIDI velocity (0-127) to OPL Total Level (63-0)
    // Formula: 63 - (velocity / 2)
    uint8_t vol = 63 - (velocity >> 1);
    
    static const uint8_t mod_offsets[] = {0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12};
    static const uint8_t car_offsets[] = {0x03,0x04,0x05,0x0B,0x0C,0x0D,0x13,0x14,0x15};
    
    // Write to Carrier (this affects the audible volume most)
    // Mask with 0xC0 to preserve Key Scale Level bits
    opl_write(0x40 + car_offsets[chan], (shadow_ksl_c[chan] & 0xC0) | vol);
}

void opl_init() {
    // 1. Silence all 9 channels immediately (Key-Off)
    // Register 0xB0-0xB8 controls Key-On
    for (uint8_t i = 0; i < 9; i++) {
        opl_write(0xB0 + i, 0x00);
        shadow_b0[i] = 0;
    }

    // 2. Wipe every OPL2 hardware register (0x01 to 0xF5)
    // This ensures that leftovers from a previous program 
    // (like long Release times or weird Waveforms) are gone.
    for (int i = 0x01; i <= 0xF5; i++) {
        opl_write(i, 0x00);
    }

    // 3. Re-enable the features we need
    opl_write(0x01, 0x20); // Enable Waveform Select
    opl_write(0xBD, 0x00); // Ensure Melodic Mode
}

void opl_silence() {
    // Just kill the 9 voices (Key-Off)
    for (uint8_t i = 0; i < 9; i++) {
        opl_write(0xB0 + i, 0x00);
        shadow_b0[i] = 0;
    }
}

uint32_t song_xram_ptr = 0;
uint16_t wait_ticks = 0;

void update_song() {
    // 1. If we are currently in a delay, count down and exit.
    if (wait_ticks > 0) {
        wait_ticks--;
        return;
    }

    // 2. We are at a tick where events need to play.
    while (1) {
        RIA.addr0 = song_xram_ptr;
        RIA.step0 = 1;

        uint8_t type = RIA.rw0;
        
        if (type == 0xFF) { // End of Song
            song_xram_ptr = 0;
            wait_ticks = 0;
            opl_init();
            return;
        }

        uint8_t chan = RIA.rw0;
        uint8_t note = RIA.rw0;
        uint8_t vel  = RIA.rw0;
        
        // Read 16-bit delay to wait AFTER this note
        uint8_t d_lo = RIA.rw0;
        uint8_t d_hi = RIA.rw0;
        uint16_t delta_after = (d_hi << 8) | d_lo;

        // Process Note
        switch(type) {
            case 0: OPL_NoteOff(chan); break;
            case 1: OPL_SetVolume(chan, vel); OPL_NoteOn(chan, note); break;
            case 3: OPL_SetPatch(chan, &gm_bank[note]); break;
        }

        // Advance to NEXT 6-byte event
        song_xram_ptr += 6;

        // If the Python script said to wait after this note, set timer and exit loop
        if (delta_after > 0) {
            wait_ticks = delta_after;
            return; 
        }
        
        // If delta_after is 0, the 'while' loop continues to play simultaneous notes
    }
}