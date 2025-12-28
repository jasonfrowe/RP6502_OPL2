#ifndef OPL_H
#define OPL_H

#define OPL_ADDR 0xFF00

#define MUSIC_FILENAME "music.bin"

typedef struct {
    uint16_t delay_ms; 
    uint8_t type;      // 0: Off, 1: On, 3: Patch
    uint8_t channel;   
    uint8_t note;      
    uint8_t velocity;  // Unused for now
} SongEvent;

extern uint8_t shadow_b0[9]; 
extern uint8_t shadow_ksl_m[9];
extern uint8_t shadow_ksl_c[9];

extern uint16_t current_event_idx;
extern uint16_t ticks_until_next_event;

extern void OPL_NoteOn(uint8_t channel, uint8_t midi_note);
extern void OPL_NoteOff(uint8_t channel);
extern void opl_clear();
extern void opl_write(uint8_t reg, uint8_t value);
extern void update_music();
extern void OPL_SetVolume(uint8_t chan, uint8_t velocity);
extern void opl_init();
extern void opl_fifo_clear();
extern void opl_silence_all();
extern void OPL_Config(uint8_t enable, uint16_t addr);
extern void music_refill_buffer();
// extern void debug_test_lseek();
// extern void shutdown_audio();

extern void music_init(const char* filename);

#endif // OPL_H