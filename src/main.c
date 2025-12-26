#include <rp6502.h>
#include <stdio.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"

// VSYNC tracking
uint8_t vsync_last = 0;

int main() {


    // Give the OPL2 FPGA core a moment to stabilize (to be tuned later)
    for (int i = 0; i < 60; i++) {
        uint8_t v = RIA.vsync;
        while (v == RIA.vsync);
    }
    opl_clear();
    opl_write(0x01, 0x20); // Enable OPL2 waveforms

    vsync_last = RIA.vsync;

    while (1) {
        // Wait for VSync increment
        while (RIA.vsync == vsync_last);
        vsync_last = RIA.vsync;

        // Run music logic exactly once per frame (60Hz)
        update_song();
        
        // Your game logic goes here
    }
}
