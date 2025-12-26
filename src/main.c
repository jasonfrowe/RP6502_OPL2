#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"


bool music_enabled = true;

int main() {
    // 1. Hardware Warmup
    for (int i = 0; i < 60; i++) {
        uint8_t v = RIA.vsync;
        while (v == RIA.vsync);
    }

    opl_init(); 
    opl_write(0x01, 0x20); // Waveforms on

    uint8_t vsync_last = RIA.vsync;

    while (1) {
        // 2. Wait for exactly one VSync (60Hz heartbeat)
        while (RIA.vsync == vsync_last);
        vsync_last = RIA.vsync;

        // 3. Update the song once per frame
        update_song();
        update_song();
        // update_song();
    }
}