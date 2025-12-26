import mido
import struct
import sys

# Standard 60Hz VSync
VSYNC_RATE = 120 

def convert_midi(midi_path, out_path):
    mid = mido.MidiFile(midi_path)
    output = bytearray()
    
    # Track absolute time in seconds
    abs_time_sec = 0.0
    last_event_vsync = 0
    events = []
    
    # 1. Capture events with absolute VSync times2tamps
    for msg in mid:
        abs_time_sec += msg.time
        if msg.is_meta: continue
        
        cmd = None
        if msg.type == 'note_on' and msg.velocity > 0:
            cmd, d1, d2 = 1, msg.note, msg.velocity
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            cmd, d1, d2 = 0, msg.note, 0
        elif msg.type == 'program_change':
            cmd, d1, d2 = 3, msg.program, 0
            
        if cmd is not None:
            v_now = round(abs_time_sec * VSYNC_RATE)
            delta = max(0, v_now - last_event_vsync)
            
            chan = msg.channel
            target_chan = 8 if chan == 9 else (chan % 8)
            
            events.append({'type': cmd, 'chan': target_chan, 'note': d1, 'vel': d2, 'delta': delta})
            last_event_vsync = v_now

    # 2. Convert to "Wait-AFTER-Play" format: [Type][Chan][Note][Vel][Wait_Next (16-bit)]
    for i in range(len(events)):
        # Delta to wait AFTER this event
        wait_after = events[i+1]['delta'] if (i+1 < len(events)) else 0
        
        output.extend(struct.pack('<BBBBH', 
            events[i]['type'], 
            events[i]['chan'], 
            events[i]['note'], 
            events[i]['vel'], 
            wait_after))

    # End Sentinel
    output.extend(struct.pack('<BBBBH', 0xFF, 0, 0, 0, 0))
    
    with open(out_path, 'wb') as f:
        f.write(output)
    print(f"Converted {len(events)} events at", VSYNC_RATE, "Hz.")

if __name__ == "__main__":
    convert_midi(sys.argv[1], sys.argv[2])