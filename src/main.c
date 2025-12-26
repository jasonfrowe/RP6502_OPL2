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

uint8_t missed_frames = 0;

int main() {
    // ... Hardware Startup ...
    opl_init(); 
    opl_write(0x01, 0x20);

    uint8_t vsync_last = RIA.vsync;
    uint16_t timer_accumulator = 0;

    while (1) {
        // Wait for VSync (60Hz)
        while (RIA.vsync == vsync_last);

        // Check if we missed a frame (vsync jumped by more than 1)
        if ((uint8_t)(RIA.vsync - vsync_last) > 1) {
            missed_frames++;
            printf("Missed Frames: %lu\n", missed_frames);
            // If this increments, your OPL2 engine is too heavy!
        }
        
        vsync_last = RIA.vsync;


        if (music_enabled) {
            // Add the song's frequency to our bucket
            timer_accumulator += SONG_HZ;

            // While we have at least one 60Hz frame's worth of time in the bucket
            // (60 is the hardware VSync rate)
            while (timer_accumulator >= 60) {
                update_song();
                timer_accumulator -= 60;
            }
        }
        
        // Game logic here
    }
}