import mido
import struct
import sys
import statistics

# Configuration
VSYNC_RATE = 120 
MAX_SIZE = 50 * 1024  # 50KB Safety Limit
FNUM_TABLE = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]

class VoiceManager:
    def __init__(self, count=9):
        self.count = count
        self.voices = [[-1, -1] for _ in range(count)] # [midi_note, midi_chan]
        self.hardware_patches = [-1] * count
        self.channel_programs = [0] * 16 

    def get_voice(self, note, chan):
        for i in range(self.count):
            if self.voices[i][0] == note and self.voices[i][1] == chan: return i
        for i in range(self.count):
            if self.voices[i][0] == -1:
                self.voices[i] = [note, chan]
                return i
        self.voices[0] = [note, chan] # Steal oldest
        return 0

    def release_voice(self, note, chan):
        for i in range(self.count):
            if self.voices[i][0] == note and self.voices[i][1] == chan:
                self.voices[i] = [-1, -1]
                return i
        return -1

def get_opl_freq(midi_note):
    n = max(12, min(midi_note, 107))
    block = (n - 12) // 12
    fnum = FNUM_TABLE[(n - 12) % 12]
    # Bit 5 is KeyOn (0x20)
    return fnum & 0xFF, 0x20 | (block << 2) | ((fnum >> 8) & 0x03)

def convert(midi_path, out_path):
    try:
        mid = mido.MidiFile(midi_path)
    except Exception as e:
        print(f"Error: {e}")
        return

    vm = VoiceManager(9)
    events = []
    v_acc = 0.0
    last_v = 0

    # Timing Analysis for the user
    deltas = [msg.time for msg in mid if msg.time > 0]
    if deltas:
        print(f"MIDI Analysis: Median gap {statistics.median(deltas):.4f}s -> ~{1.0/statistics.median(deltas):.1f}Hz")

    for msg in mid:
        v_acc += msg.time * VSYNC_RATE
        if msg.is_meta: continue

        m_chan = getattr(msg, 'channel', 0)
        
        if msg.type == 'program_change':
            vm.channel_programs[m_chan] = msg.program
            continue

        if msg.type == 'note_on' and msg.velocity > 0:
            v_now = round(v_acc)
            delta = max(0, v_now - last_v)
            last_v = v_now

            target_opl_chan = vm.get_voice(msg.note, m_chan)
            prog = vm.channel_programs[m_chan]

            # Drum IDs 128-130
            if m_chan == 9:
                if msg.note in [35, 36]: prog = 128
                elif msg.note in [38, 40]: prog = 129
                else: prog = 130
                f_low, f_high = get_opl_freq(60) 
            else:
                f_low, f_high = get_opl_freq(msg.note)

            if vm.hardware_patches[target_opl_chan] != prog:
                events.append({'type': 3, 'chan': target_opl_chan, 'd1': prog, 'd2': 0, 'delta': delta})
                vm.hardware_patches[target_opl_chan] = prog
                delta = 0 

            events.append({'type': 1, 'chan': target_opl_chan, 'd1': f_low, 'd2': f_high, 'delta': delta})

        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            target_opl_chan = vm.release_voice(msg.note, m_chan)
            if target_opl_chan != -1:
                v_now = round(v_acc)
                delta = max(0, v_now - last_v)
                last_v = v_now
                events.append({'type': 0, 'chan': target_opl_chan, 'd1': 0, 'd2': 0, 'delta': delta})

    # Binary Output Loop with 50KB Limit
    output = bytearray()
    for i in range(len(events)):
        # Check if adding this 6-byte event exceeds 50KB
        if len(output) >= MAX_SIZE - 6:
            print(f"LIMIT REACHED: Truncating song at {len(output)} bytes.")
            break
            
        d_after = events[i+1]['delta'] if (i+1 < len(events)) else 0
        output.extend(struct.pack('<BBBBH', 
            events[i]['type'], 
            events[i]['chan'], 
            events[i]['d1'], 
            events[i]['d2'], 
            d_after))
    
    # Always append End Marker
    output.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))
    
    with open(out_path, 'wb') as f:
        f.write(output)
    print(f"Converted {len(output)//6} events to {out_path} ({len(output)} bytes)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 midi2pix.py input.mid output.bin")
    else:
        convert(sys.argv[1], sys.argv[2])