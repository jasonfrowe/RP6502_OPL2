#include <rp6502.h>
#include <6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"

// Enable Music Playback
bool music_enabled = true;

// Use 'volatile' for variables shared between IRQ and Main (if needed)
// Use 'static' if only the IRQ uses them to preserve state between calls
static uint16_t timer_accumulator = 0;

// Change this to match the VSYNC_RATE in your Python script
#define SONG_HZ 120 

// Manual definitions for CPU control
#define cli() __asm__ volatile ("cli" ::: "memory")
#define sei() __asm__ volatile ("sei" ::: "memory")

// 2. Define the Hardware IRQ Vector ($FFFE)
// This tells the RIA: "When an IRQ happens, jump to this address"
#define RIA_IRQ_VEC (*(uint16_t*)0xFFFE)

// This is the global pointer the RP6502 SDK uses for interrupts
extern void (*irq_handler)(void);

// Your music/vsync handler
__attribute__((interrupt)) void my_vsync_handler(void) {
    uint8_t status = RIA.irq; 
    
    if (status & 1) { // 60Hz VSync pulse
        // 1. Add the song rate to the bucket
        timer_accumulator += SONG_HZ;

        // 2. Catch up: process as many ticks as needed for this frame
        // For 120Hz, this loop will run exactly twice every frame.
        // For 140Hz, it will run twice most frames and three times every few frames.
        while (timer_accumulator >= 60) {
            update_midi_song(); // MUST use RIA Port 1 (addr1/rw1)
            timer_accumulator -= 60;
        }
    }
}


int main() {
    // 1. Setup Vector and Hardware
    RIA_IRQ_VEC = (uint16_t)my_vsync_handler;

    OPL_Config(1, OPL_ADDR);

    opl_init(); 
    
    // 2. Load song to XRAM...
    
    // 3. Enable Interrupts
    RIA.irq = 1; 
    cli(); 

    while (1) {

        // --- GAME LOOP ---
        // You can use a flag to stay in sync with VSync for graphics:
        // if (RIA.vsync != vsync_last) {
        //     vsync_last = RIA.vsync;
        //     move_sprites();
        //     draw_screen();
        // }
        
        // Or just run free! The music is safe in the background.
    }
}