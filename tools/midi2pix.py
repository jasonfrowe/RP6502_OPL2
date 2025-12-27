import mido
import struct
import sys

# --- CONFIGURATION ---
VSYNC_RATE = 120    
MAX_SIZE = 50 * 1024 
FNUM_TABLE = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]

class VoiceManager:
    def __init__(self, count=9):
        self.count = count
        # [midi_note, midi_channel] for each OPL hardware voice
        self.opl_voices = [[-1, -1] for _ in range(count)]
        # Which Patch ID (0-131) is currently loaded in each OPL hardware voice
        self.hw_patch_cache = [-1] * count
        # Which MIDI Program is currently selected for each MIDI channel (0-15)
        self.midi_prog_cache = [0] * 16

    def get_opl_chan(self, midi_note, midi_chan):
        # 1. Reuse if already playing
        for i in range(self.count):
            if self.opl_voices[i][0] == midi_note and self.opl_voices[i][1] == midi_chan:
                return i
        # 2. Find empty channel
        for i in range(self.count):
            if self.opl_voices[i][0] == -1:
                self.opl_voices[i] = [midi_note, midi_chan]
                return i
        # 3. Steal oldest (0)
        self.opl_voices[0] = [midi_note, midi_chan]
        return 0

    def kill_opl_chan(self, midi_note, midi_chan):
        for i in range(self.count):
            if self.opl_voices[i][0] == midi_note and self.opl_voices[i][1] == midi_chan:
                self.opl_voices[i] = [-1, -1]
                return i
        return -1

def get_opl_freq(midi_note):
    n = max(12, min(midi_note, 107))
    block = (n - 12) // 12
    fnum = FNUM_TABLE[(n - 12) % 12]
    return fnum & 0xFF, (0x20 | (block << 2) | ((fnum >> 8) & 0x03))

def convert(midi_path, out_path):
    try:
        mid = mido.MidiFile(midi_path)
    except Exception as e:
        print(f"Error: {e}"); return

    vm = VoiceManager(9)
    events = []
    v_acc = 0.0
    last_v = 0

    # Sort all messages by absolute time
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
                # Assign to an OPL voice
                tc = vm.get_opl_chan(msg.note, m_chan)
                
                # Determine what instrument this MIDI channel needs
                if m_chan == 9: # MIDI Drum Channel
                    if msg.note in [35, 36]:   prog = 128 # BD
                    elif msg.note in [38, 40]: prog = 129 # SD
                    elif msg.note in [42, 44]: prog = 130 # HH
                    else:                      prog = 131 # PH
                    f_low, f_high = get_opl_freq(60) # Fixed drum pitch
                else:
                    prog = vm.midi_prog_cache[m_chan]
                    f_low, f_high = get_opl_freq(msg.note)

                # --- LAZY PATCHING ---
                # Only inject a patch change if the OPL channel doesn't have it
                if vm.hw_patch_cache[tc] != prog:
                    events.append({'type': 3, 'chan': tc, 'd1': prog, 'd2': 0, 'delta': delta})
                    vm.hw_patch_cache[tc] = prog
                    delta = 0 # Play note immediately after patch change

                events.append({'type': 1, 'chan': tc, 'd1': f_low, 'd2': f_high, 'delta': delta})

            else: # Note Off
                tc = vm.kill_opl_chan(msg.note, m_chan)
                if tc != -1:
                    events.append({'type': 0, 'chan': tc, 'd1': 0, 'd2': 0, 'delta': delta})

    # Binary Serializer (6-byte format)
    output = bytearray()
    for i in range(len(events)):
        if len(output) >= MAX_SIZE - 6: break
        # Delay to wait AFTER playing this event
        d_after = events[i+1]['delta'] if (i+1 < len(events)) else 0
        output.extend(struct.pack('<BBBBH', 
            events[i]['type'], events[i]['chan'], 
            events[i]['d1'], events[i]['d2'], d_after))

    output.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))
    with open(out_path, 'wb') as f: f.write(output)
    print(f"Converted {len(events)} events to {out_path}")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])