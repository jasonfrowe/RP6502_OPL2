import mido
import struct
import sys
import statistics

VSYNC_RATE = 120 
MAX_SIZE = 50 * 1024

class VoiceManager:
    def __init__(self, count=9):
        self.count = count
        # [midi_note, midi_channel]
        self.voices = [[-1, -1] for _ in range(count)]
        # Track which MIDI Program (0-127) is currently loaded in each OPL hardware slot
        self.opl_hardware_patches = [-1] * count
        # Track which Program is selected for each MIDI channel
        self.midi_channel_programs = [0] * 16 

    def get_voice(self, midi_note, midi_chan):
        # 1. Reuse channel if same note/chan is already there
        for i in range(self.count):
            if self.voices[i][0] == midi_note and self.voices[i][1] == midi_chan:
                return i
        # 2. Find empty
        for i in range(self.count):
            if self.voices[i][0] == -1:
                self.voices[i] = [midi_note, midi_chan]
                return i
        # 3. Steal oldest (fallback)
        self.voices[0] = [midi_note, midi_chan]
        return 0

    def release_voice(self, midi_note, midi_chan):
        for i in range(self.count):
            if self.voices[i][0] == midi_note and self.voices[i][1] == midi_chan:
                self.voices[i] = [-1, -1]
                return i
        return -1

def convert(midi_path, out_path):
    mid = mido.MidiFile(midi_path)
    
    # Timing Analysis
    deltas = [msg.time for msg in mid if msg.time > 0]
    if deltas:
        print(f"Analysis: Median gap {statistics.median(deltas):.4f}s -> ~{1.0/statistics.median(deltas):.1f}Hz")

    output = bytearray()
    vm = VoiceManager(9)
    v_acc = 0.0
    last_v = 0
    events = []

    for msg in mid:
        v_acc += msg.time * VSYNC_RATE
        v_now = round(v_acc)
        delta = max(0, v_now - last_v)

        if msg.is_meta:
            continue

        m_chan = getattr(msg, 'channel', 0)
        
        if msg.type == 'program_change':
            # Just update our internal MIDI channel state, don't write to OPL yet
            vm.midi_channel_programs[m_chan] = msg.program
            continue

        if msg.type == 'note_on' and msg.velocity > 0:
            target_opl_chan = vm.get_voice(msg.note, m_chan)
            
            needed_program = vm.midi_channel_programs[m_chan]
            
            # --- IMPROVED DRUM MAPPING ---
            if m_chan == 9: # MIDI Channel 10
                if msg.note in [35, 36]: 
                    needed_program = 128 # Bass Drum
                elif msg.note in [38, 40]: 
                    needed_program = 129 # Snare
                elif msg.note in [42, 44, 46]: 
                    needed_program = 130 # Hi-Hat
                else: 
                    needed_program = 131 # Generic Percussion

            # If the OPL hardware channel doesn't have this "drum" or "instrument" loaded
            if vm.opl_hardware_patches[target_opl_chan] != needed_program:
                events.append({'type': 3, 'chan': target_opl_chan, 'note': needed_program, 'vel': 0, 'delta': delta})
                vm.opl_hardware_patches[target_opl_chan] = needed_program
                delta = 0 
            
            events.append({'type': 1, 'chan': target_opl_chan, 'note': msg.note, 'vel': msg.velocity, 'delta': delta})
            last_v = v_now

        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            target_opl_chan = vm.release_voice(msg.note, m_chan)
            if target_opl_chan != -1:
                events.append({'type': 0, 'chan': target_opl_chan, 'note': msg.note, 'vel': 0, 'delta': delta})
                last_v = v_now

    # Serialize to binary [Type][Chan][Note][Vel][Wait_After (16-bit)]
    for i in range(len(events)):
        if len(output) >= MAX_SIZE - 6: break
        d_after = events[i+1]['delta'] if (i+1 < len(events)) else 0
        output.extend(struct.pack('<BBBBH', 
            events[i]['type'], events[i]['chan'], 
            events[i]['note'], events[i]['vel'], d_after))

    output.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))
    with open(out_path, 'wb') as f: f.write(output)
    print(f"Converted {len(events)} events.")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])