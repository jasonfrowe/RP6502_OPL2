#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"


bool music_enabled = true;

// Change this to match the VSYNC_RATE in your Python script
#define SONG_HZ 120 


int main() {
    // ... Hardware Startup ...
    opl_fifo_clear(); 
    opl_init(); 
    opl_write(0x01, 0x20);

    uint8_t vsync_last = RIA.vsync;
    uint16_t timer_accumulator = 0;

    while (1) {

        if (RIA.vsync == vsync_last)
            continue;
        
        vsync_last = RIA.vsync;


        if (music_enabled) {
        timer_accumulator += SONG_HZ;
        while (timer_accumulator >= 60) {
            update_song();
            timer_accumulator -= 60;
        }
}
        
        // Game logic here
    }
}