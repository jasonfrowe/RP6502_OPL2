#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "opl.h"
#include "instruments.h"

#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

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
    RIA.addr1 = OPL_ADDR; // OPL Write Index
    RIA.step1 = 1;
    
    RIA.rw1 = reg;   // Write Index (FF00)
    RIA.rw1 = data;  // Write Data  (FF01)
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
    RIA.addr1 = OPL_ADDR + 2; // Our new FIFO flush register
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

void opl_fifo_flush() {
    // Ensure the Magic Key (0xAA) matches our Verilog flush logic
    RIA.addr1 = OPL_ADDR + 2;
    RIA.step1 = 0;
    RIA.rw1 = 0xAA; 
}

void shutdown_audio() {
    opl_silence_all();       // Kill any playing notes
    opl_fifo_flush();        // Clear the hardware buffer
    OPL_Config(0, OPL_ADDR);   // Tell the FPGA to stop listening to the PIX bus
}


void OPL_Config(uint8_t enable, uint16_t addr) {
    // Configure OPL Device in FPGA

    // Args: dev(1), chan(0), reg(9), count(3)
    xregn(2, 0, 0, 2, enable, addr);
    
}

static int music_fd = -1;
static uint8_t music_buffer[512];
static uint16_t music_buf_idx = 0;
static uint16_t music_bytes_ready = 0; 
static uint16_t music_wait_ticks = 0;
static bool music_error_state = false;

void music_init(const char* filename) {
    if (music_fd >= 0) close(music_fd);
    music_fd = open(filename, O_RDONLY);
    
    music_buf_idx = 0;
    music_wait_ticks = 0;
    music_error_state = (music_fd < 0);

    if (music_error_state) {
        printf("Music: Failed to open %s\n", filename);
        return;
    }

    // 1. Read into the START of the buffer (index 0)
    int res = read(music_fd, music_buffer, 512);
                
    if (res < 0) {
        int err = errno;
        printf("Music: Initial Read Error %d\n", err);
        music_error_state = true;
        return;
    }

    // 2. IMPORTANT: Update the count of valid bytes in the buffer
    // Without this, the sequencer thinks the buffer is empty!
    music_bytes_ready = res; 
    
   //  printf("Music: Started. Initialized with %d bytes.\n", res);
}

void update_music() {
    if (music_error_state || music_fd < 0) return;

    if (music_wait_ticks > 0) {
        music_wait_ticks--;
    }

    if (music_wait_ticks == 0) {
        while (music_wait_ticks == 0) {

            if (music_buf_idx >= 512){
                // printf("Music: Buffer Refill Triggered.\n");
                
                int res = read(music_fd, &music_buffer, 512);
                
                if (res < 0) {
                    int err = errno;
                    printf("Music: Read Error %d\n", err);
                    music_error_state = true;
                    return;
                }

                music_buf_idx = 0;
            }

            // --- 4-BYTE PACKET ACCESS ---
            uint8_t reg  = music_buffer[music_buf_idx++];
            uint8_t val  = music_buffer[music_buf_idx++];
            uint8_t d_lo = music_buffer[music_buf_idx++];
            uint8_t d_hi = music_buffer[music_buf_idx++];
            uint16_t delay = ((uint16_t)d_hi << 8) | d_lo;


            if (reg == 0xFF && val == 0xFF) {
                off_t seek_res = lseek(music_fd, 0, SEEK_SET);
                // if (seek_res == -1) {
                //     int err = errno;
                //     printf("Music: Sentinel Lseek Error %d\n", err);
                //     music_error_state = true;
                // } else {
                //     printf("Music: Loop Sentinel Correctly Processed.\n");
                // }
                
                // probably not available in rp6502 for LLVM-MOS
                // f_lseek(music_fd, 0, SEEK_SET);

                // printf("Music: Sentinel Hit. Re-opening...\n");
                // close(music_fd);
                // music_fd = open(MUSIC_FILENAME, O_RDONLY);

                music_buf_idx = 512; // Force buffer reload
                delay = 1; // Small delay after loop

            } else {
                opl_write(reg, val);
            }

            if (delay > 0) {
                music_wait_ticks = delay;
            }

        }
    }
}

// void debug_test_lseek() {
//     uint8_t start_bytes[4];
//     uint8_t check_bytes[4];
//     off_t pos;
    
//     // 1. Read first 4 bytes
//     // pos = lseek(music_fd, 0, SEEK_SET);
//     // printf("LSEEK TEST: Initial lseek to pos %ld\n", (long)pos);
//     read(music_fd, start_bytes, 4);
    
//     // 2. Move away and move back
//     // pos = lseek(music_fd, 1024, SEEK_SET); 
//     // printf("LSEEK TEST: Moved to pos %ld\n", (long)pos);
//     pos = lseek(music_fd, 0, SEEK_SET); 
//     printf("LSEEK TEST: Returned to pos %ld\n", (long)pos);
    
//     // 3. Read again
//     read(music_fd, check_bytes, 4);
    
//     printf("LSEEK TEST: First Read: %02X %02X %02X %02X\n", 
//             start_bytes[0], start_bytes[1], start_bytes[2], start_bytes[3]);
//     printf("LSEEK TEST: After Seek: %02X %02X %02X %02X\n", 
//             check_bytes[0], check_bytes[1], check_bytes[2], check_bytes[3]);
            
//     if (start_bytes[0] == check_bytes[0] && start_bytes[1] == check_bytes[1]) {
//         printf("RESULT: lseek is working correctly.\n");
//     } else {
//         printf("RESULT: lseek FAILED or returned inconsistent data!\n");
//     }
// }