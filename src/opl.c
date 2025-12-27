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

uint8_t channel_is_drum[9] = {0,0,0,0,0,0,0,0,0}; 

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
    
    RIA.rw0 = reg;   // Write Index (FF00)
    RIA.rw0 = data;  // Write Data  (FF01)
    // Any delays are now handled by the FIFO in hardware
}

void opl_silence_all() {
    // Send Note-Off to all 9 channels
    // We let these go through the FIFO so they are timed correctly
    for (uint8_t i = 0; i < 9; i++) {
        opl_write(0xB0 + i, 0x00);
    }
}

void opl_fifo_clear() {
    RIA.addr1 = 0xFF02; // Our new FIFO flush register
    RIA.step1 = 0;
    RIA.rw1 = 1;         // Trigger flush
}

void OPL_NoteOn(uint8_t channel, uint8_t midi_note) {
    if (channel > 8) return;

    // If this channel is currently a drum, force the pitch to Middle C (60)
    // This makes FM patches sound like drums instead of weird low bloops.
    if (channel_is_drum[channel]) {
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

    for (int i = 0; i < 9; i++) {
        channel_is_drum[i] = 0;
        shadow_b0[i] = 0;
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
    if (wait_ticks > 0) {
        wait_ticks--;
        return;
    }

    while (1) {
        RIA.addr0 = song_xram_ptr;
        RIA.step0 = 1;

        uint8_t type = RIA.rw0;
        if (type == 0xFF) { 
            song_xram_ptr = 0; wait_ticks = 0;
            opl_silence_all(); // Kill hanging notes
            return; 
        }

        uint8_t chan = RIA.rw0;
        uint8_t d1   = RIA.rw0; // Pre-calculated f_low OR Patch ID
        uint8_t d2   = RIA.rw0; // Pre-calculated f_high
        
        uint8_t d_lo = RIA.rw0;
        uint8_t d_hi = RIA.rw0;
        uint16_t delta_after = (d_hi << 8) | d_lo;

        switch(type) {
            case 0: // Note Off
                opl_write(0xB0 + chan, 0x00); 
                break;
            case 1: // Note On
                opl_write(0xA0 + chan, d1);
                opl_write(0xB0 + chan, d2);
                break;
            case 3: // Patch Change
                if (d1 == 128) OPL_SetPatch(chan, &drum_bd);
                else if (d1 == 129) OPL_SetPatch(chan, &drum_snare);
                else if (d1 == 130) OPL_SetPatch(chan, &drum_hihat);
                else OPL_SetPatch(chan, &gm_bank[d1]);
                break;
        }

        song_xram_ptr += 6;

        if (delta_after > 0) {
            wait_ticks = delta_after;
            return; 
        }
    }
}