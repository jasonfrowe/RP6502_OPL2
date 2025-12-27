import mido
import struct
import sys
import statistics

# --- CONFIGURATION ---
VSYNC_RATE = 120    # Match SONG_HZ in your C code
MAX_SIZE = 50 * 1024 # 50KB XRAM Safety Limit

# F-Number table for Octave 4 @ 4.0 MHz
FNUM_TABLE = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]

class VoiceManager:
    def __init__(self, count=9):
        self.count = count
        # Each slot: [midi_note, midi_channel]
        self.voices = [[-1, -1] for _ in range(count)]
        # Track which patch is currently loaded in OPL hardware (Lazy Patching)
        self.opl_hardware_patches = [-1] * count
        # Track which program is assigned to each MIDI channel (0-15)
        self.midi_channel_programs = [0] * 16 

    def get_voice(self, note, chan):
        # 1. Reuse channel if same note/chan is already there (prevents double-triggering)
        for i in range(self.count):
            if self.voices[i][0] == note and self.voices[i][1] == chan:
                return i
        # 2. Find an empty OPL channel
        for i in range(self.count):
            if self.voices[i][0] == -1:
                self.voices[i] = [note, chan]
                return i
        # 3. All busy: Steal oldest (fallback to channel 0)
        self.voices[0] = [note, chan]
        return 0

    def release_voice(self, note, chan):
        # Find which OPL channel was playing this specific MIDI note
        for i in range(self.count):
            if self.voices[i][0] == note and self.voices[i][1] == chan:
                self.voices[i] = [-1, -1]
                return i
        return -1

def get_opl_freq(midi_note):
    """Pre-calculates OPL2 F-Number and Block for the 6502."""
    n = max(12, min(midi_note, 107))
    block = (n - 12) // 12
    fnum = FNUM_TABLE[(n - 12) % 12]
    # Returns (Low Byte for A0-A8, High Byte with KeyOn 0x20 for B0-B8)
    return fnum & 0xFF, (0x20 | (block << 2) | ((fnum >> 8) & 0x03))

def convert(midi_path, out_path):
    try:
        mid = mido.MidiFile(midi_path)
    except Exception as e:
        print(f"Error: {e}")
        return

    # Timing Analysis
    deltas = [msg.time for msg in mid if msg.time > 0]
    if deltas:
        print(f"MIDI Analysis: Median gap {statistics.median(deltas):.4f}s -> ~{1.0/statistics.median(deltas):.1f}Hz")

    vm = VoiceManager(9)
    output_events = []
    v_acc = 0.0
    last_v = 0
    
    # 'for msg in mid' merges tracks and handles tempo automatically
    for msg in mid:
        v_acc += msg.time * VSYNC_RATE
        if msg.is_meta:
            continue

        m_chan = getattr(msg, 'channel', 0)
        
        # 1. Update MIDI channel programs (Instrument changes)
        if msg.type == 'program_change':
            vm.midi_channel_programs[m_chan] = msg.program
            continue

        # 2. Handle Note On
        if msg.type == 'note_on' and msg.velocity > 0:
            v_now = round(v_acc)
            delta = max(0, v_now - last_v)
            last_v = v_now

            target_opl_chan = vm.get_voice(msg.note, m_chan)
            
            # Identify the required patch
            # MIDI 10 (Index 9) -> OPL Drum Patches
            if m_chan == 9:
                if msg.note in [35, 36]:   needed_prog = 128 # BD
                elif msg.note in [38, 40]: needed_prog = 129 # SD
                elif msg.note in [42, 44]: needed_prog = 130 # CH
                else:                      needed_prog = 131 # PH
                # Drums always play at a neutral pitch (Middle C)
                f_low, f_high = get_opl_freq(60)
            else:
                needed_prog = vm.midi_channel_programs[m_chan]
                f_low, f_high = get_opl_freq(msg.note)

            # --- LAZY PATCHING ---
            # If this OPL channel doesn't have the right instrument, inject a Patch Change
            if vm.opl_hardware_patches[target_opl_chan] != needed_prog:
                output_events.append({'type': 3, 'chan': target_opl_chan, 'd1': needed_prog, 'd2': 0, 'delta': delta})
                vm.opl_hardware_patches[target_opl_chan] = needed_prog
                delta = 0 # Ensure NoteOn follows immediately

            output_events.append({'type': 1, 'chan': target_opl_chan, 'd1': f_low, 'd2': f_high, 'delta': delta})

        # 3. Handle Note Off
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            target_opl_chan = vm.release_voice(msg.note, m_chan)
            if target_opl_chan != -1:
                v_now = round(v_acc)
                delta = max(0, v_now - last_v)
                last_v = v_now
                # d1 and d2 are unused for NoteOff
                output_events.append({'type': 0, 'chan': target_opl_chan, 'd1': 0, 'd2': 0, 'delta': delta})

    # --- BINARY SERIALIZATION ---
    binary_data = bytearray()
    for i in range(len(output_events)):
        # Check XRAM limit
        if len(binary_data) >= MAX_SIZE - 6:
            print(f"TRUNCATED: Song hit {MAX_SIZE//1024}KB limit.")
            break
            
        # Delta belongs to the NEXT event (Wait-After-Play)
        d_after = output_events[i+1]['delta'] if (i+1 < len(output_events)) else 0
        
        # Format: [Type][Chan][Data1][Data2][Wait_After(16-bit)] = 6 bytes
        binary_data.extend(struct.pack('<BBBBH', 
            output_events[i]['type'], 
            output_events[i]['chan'], 
            output_events[i]['d1'], 
            output_events[i]['d2'], 
            d_after))

    # Add End Sentinel
    binary_data.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))

    with open(out_path, 'wb') as f:
        f.write(binary_data)
    print(f"Success: {len(output_events)} events -> {out_path} ({len(binary_data)} bytes)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 midi2pix.py input.mid output.bin")
    else:
        convert(sys.argv[1], sys.argv[2])