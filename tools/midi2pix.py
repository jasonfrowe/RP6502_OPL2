import mido
import struct
import sys

# 60Hz VSync timing
VSYNC_RATE = 60 

def convert_midi(midi_path, out_path):
    try:
        mid = mido.MidiFile(midi_path)
    except Exception as e:
        print(f"Error: {e}")
        return

    output = bytearray()
    
    # 1. Using 'for msg in mid:' automatically:
    # - Merges all tracks in chronological order
    # - Converts msg.time from MIDI Ticks to real Seconds
    # - Handles all tempo changes (BPM) for us
    
    v_accumulated = 0.0 # Total VSyncs passed
    last_v_rounded = 0
    events = []
    
    for msg in mid:
        v_accumulated += msg.time * VSYNC_RATE
        
        cmd = None
        if msg.type == 'note_on' and msg.velocity > 0:
            cmd, d1, d2 = 1, msg.note, msg.velocity
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            cmd, d1, d2 = 0, msg.note, 0
        elif msg.type == 'program_change':
            cmd, d1, d2 = 3, msg.program, 0
            
        if cmd is not None:
            v_now = round(v_accumulated)
            delta = max(0, v_now - last_v_rounded)
            
            # Channel Mapping: MIDI 10 -> OPL 8. Others 0-7.
            chan = msg.channel if hasattr(msg, 'channel') else 0
            target_chan = 8 if chan == 9 else (chan % 8)
            
            events.append({'type': cmd, 'chan': target_chan, 'note': d1, 'vel': d2, 'delta': delta})
            last_v_rounded = v_now

    # 2. Wait-After-Play Shift: Attach delta to the PREVIOUS event.
    for i in range(len(events)):
        # How many frames to wait AFTER playing event[i] before playing event[i+1]
        d_after = events[i+1]['delta'] if (i+1 < len(events)) else 0
        
        # Binary: [Type][Chan][Note][Vel] [Wait_After (16-bit LE)]
        output.extend(struct.pack('<BBBBH', 
            events[i]['type'], 
            events[i]['chan'], 
            events[i]['note'], 
            events[i]['vel'], 
            d_after))

    # 3. Add End Marker
    output.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))
    
    with open(out_path, 'wb') as f:
        f.write(output)
    print(f"Success! {len(events)} events converted.")

if __name__ == "__main__":
    convert_midi(sys.argv[1], sys.argv[2])