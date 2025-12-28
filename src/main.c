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

int main() {
    OPL_Config(1, 0xFF00);
    opl_init();
    music_init("music.bin");

    vsync_last = RIA.vsync;

    while (1) {
        // --- 1. THE ONLY SYNC POINT ---
        if (RIA.vsync == vsync_last) continue;
        vsync_last = RIA.vsync;

        // --- 2. THE MUSIC WORK ---
        // This is safe because it's the start of the frame.
        // If update_song calls read(), it happens now.
        timer_accumulator += SONG_HZ;
        while (timer_accumulator >= 60) {
            update_song();
            timer_accumulator -= 60;
        }

        // --- 3. THE GAME WORK ---
        // Everything else happens AFTER the music is done.
        // run_game_logic();
    }
}