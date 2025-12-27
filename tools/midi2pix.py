import mido
import struct
import sys
import statistics

# --- CONFIGURATION ---
VSYNC_RATE = 120    
MAX_SIZE = 50 * 1024 
FNUM_TABLE = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]

class VoiceManager:
    def __init__(self, count=9):
        self.count = count
        # [midi_note, midi_channel, age_counter]
        self.voices = [[-1, -1, 0] for _ in range(count)]
        self.hw_patch_cache = [-1] * count
        self.midi_prog_cache = [0] * 16
        self.timer = 0

    def get_opl_chan(self, note, chan):
        self.timer += 1
        # 1. Reuse if already playing
        for i in range(self.count):
            if self.voices[i][0] == note and self.voices[i][1] == chan:
                self.voices[i][2] = self.timer
                return i, False # No need to force-kill
        
        # 2. Find empty channel
        for i in range(self.count):
            if self.voices[i][0] == -1:
                self.voices[i] = [note, chan, self.timer]
                return i, False
        
        # 3. Steal the oldest (LRU)
        oldest_idx = 0
        oldest_age = self.timer
        for i in range(self.count):
            if self.voices[i][2] < oldest_age:
                oldest_age = self.voices[i][2]
                oldest_idx = i
        
        self.voices[oldest_idx] = [note, chan, self.timer]
        return oldest_idx, True # MUST force-kill the old note

    def kill_opl_chan(self, note, chan):
        for i in range(self.count):
            if self.voices[i][0] == note and self.voices[i][1] == chan:
                self.voices[i][0] = -1 # Mark as free
                return i
        return -1

def get_opl_freq(midi_note):
    n = max(12, min(midi_note, 107))
    block = (n - 12) // 12
    fnum = FNUM_TABLE[(n - 12) % 12]
    return fnum & 0xFF, (0x20 | (block << 2) | ((fnum >> 8) & 0x03))

def convert(midi_path, out_path):
    mid = mido.MidiFile(midi_path)
    vm = VoiceManager(9)
    events = []
    v_acc = 0.0
    last_v = 0

    for msg in mid:
        v_acc += msg.time * VSYNC_RATE
        if msg.is_meta: continue

        m_chan = getattr(msg, 'channel', 0)
        if msg.type == 'program_change':
            vm.midi_prog_cache[m_chan] = msg.program
            continue

        if msg.type in ['note_on', 'note_off']:
            v_now = round(v_acc)
            delta = max(0, v_now - last_v)
            last_v = v_now

            if msg.type == 'note_on' and msg.velocity > 0:
                # 1. Get channel
                prog = vm.midi_prog_cache[m_chan]
                if m_chan == 9: # Percussion logic
                    if msg.note in [35, 36]:   prog = 128
                    elif msg.note in [38, 40]: prog = 129
                    else: prog = 130
                    f_low, f_high = get_opl_freq(60)
                else:
                    f_low, f_high = get_opl_freq(msg.note)

                tc, force_kill = vm.get_opl_chan(msg.note, m_chan)
                
                # 2. If stealing, send a Note-Off first
                if force_kill:
                    events.append({'type': 0, 'chan': tc, 'd1': 0, 'd2': 0, 'delta': delta})
                    delta = 0 # Ensure NoteOn follows immediately

                # 3. Context Switch Instrument
                if vm.hw_patch_cache[tc] != prog:
                    events.append({'type': 3, 'chan': tc, 'd1': prog, 'd2': 0, 'delta': delta})
                    vm.hw_patch_cache[tc] = prog
                    delta = 0 

                # 4. Note On
                events.append({'type': 1, 'chan': tc, 'd1': f_low, 'd2': f_high, 'delta': delta})

            else: # Note Off
                tc = vm.kill_opl_chan(msg.note, m_chan)
                if tc != -1:
                    events.append({'type': 0, 'chan': tc, 'd1': 0, 'd2': 0, 'delta': delta})

    # Binary Serializer
    output = bytearray()
    for i in range(len(events)):
        if len(output) >= MAX_SIZE - 6: break
        d_after = events[i+1]['delta'] if (i+1 < len(events)) else 0
        output.extend(struct.pack('<BBBBH', events[i]['type'], events[i]['chan'], 
                                           events[i]['d1'], events[i]['d2'], d_after))

    output.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))
    with open(out_path, 'wb') as f: f.write(output)

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
    print("Conversion complete.")