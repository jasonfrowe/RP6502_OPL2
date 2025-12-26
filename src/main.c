#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"

// VSYNC tracking
uint8_t vsync_last = 0;
bool music_enabled = true;

int main() {
    // 1. Hardware Startup
    // Wait for FPGA to be ready
    for (int i = 0; i < 60; i++) {
        uint8_t v = RIA.vsync;
        while (v == RIA.vsync);
    }

    // 2. Clear hardware and memory
    opl_init(); 
    // (Load instruments here if they aren't const)

    vsync_last = RIA.vsync;

    while (1) {
        while (RIA.vsync == vsync_last);
        vsync_last = RIA.vsync;

        // --- THE JUKEBOX ---
        if (music_enabled) {
            update_song();
        }

        // --- THE GAME LOGIC ---
        // if (RIA.keyboard == KEY_ESC) {
        //    music_enabled = 0;
        //    opl_silence(); // Quickly kill the music
        // }
    }
}
