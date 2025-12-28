#include <rp6502.h>
#include <6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"

#define SONG_HZ 60
uint8_t vsync_last = 0;
uint16_t timer_accumulator = 0;
bool music_enabled = true;

int main() {
    // 1. Initialize Hardware
    // (Ensure your xregn PnP call is here)
    OPL_Config(1, OPL_ADDR);
    opl_init();

    // 2. Prepare Music
    music_init("music.bin");

    vsync_last = RIA.vsync;

    while (1) {
        // --- 1. SYNC TO VSYNC ---
        if (RIA.vsync == vsync_last)
            continue;
        vsync_last = RIA.vsync;

        // --- 2. DRIVE MUSIC ---
        // This math allows any SONG_HZ to work on a 60Hz VSync
        if (music_enabled) {
            timer_accumulator += SONG_HZ;
            while (timer_accumulator >= 60) {
                update_song();
                timer_accumulator -= 60;
            }
        }

        // --- 3. YOUR GAME LOGIC ---
        // Move sprites, check keys, etc.
        // You can safely use RIA.addr0/rw0 here!
    }
}