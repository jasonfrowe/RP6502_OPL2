#include <rp6502.h>
#include <stdio.h>
#include "opl.h"
#include "instruments.h"
#include "song_data.h"

void update_song() {
    // 1. If we still have waiting time, just decrement and exit
    if (ticks_until_next_event > 0) {
        ticks_until_next_event--;
        return;
    }

    // 2. Process events that happen "now" (handles multiple events at same tick)
    while (ticks_until_next_event == 0) {
        const SongEvent* ev = &twinkle[current_event_idx];

        // Handle End of Song
        if (current_event_idx >= (sizeof(twinkle)/sizeof(SongEvent))) {
            current_event_idx = 0; // Loop or stop
            return;
        }

        switch(ev->type) {
            case 0: OPL_NoteOff(ev->channel); break;
            case 1: OPL_NoteOn(ev->channel, ev->note); break;
            case 3: OPL_SetPatch(ev->channel, &gm_bank[ev->note]); break;
        }

        current_event_idx++;
        
        // Peek at the next event's delay
        // NOTE: We convert your ms delay to VSync ticks (ms / 16.66)
        // For a 60Hz tick, 100ms is approx 6 ticks.
        ticks_until_next_event = twinkle[current_event_idx].delay_ms / 17;
    }
}

// VSYNC tracking
uint8_t vsync_last = 0;

int main() {
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
