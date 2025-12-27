import struct
import sys
import gzip

# MUST match SONG_HZ in your C code
TARGET_HZ = 60 

def convert_vgm(vgm_path, out_path):
    with (gzip.open(vgm_path, 'rb') if vgm_path.endswith('.vgz') else open(vgm_path, 'rb')) as f:
        data = f.read()

    if data[:4] != b'Vgm ':
        print("Error: Not a valid VGM file")
        return

    vgm_offset = struct.unpack('<I', data[0x34:0x38])[0] + 0x34
    
    output = bytearray()
    pending_writes = []
    
    # We use a float to track exactly how many VSync ticks have passed
    # to avoid rounding errors "eating" the rhythm.
    vsync_timer = 0.0
    last_vsync_int = 0

    i = vgm_offset
    while i < len(data):
        cmd = data[i]
        
        # OPL2 Write (0x5A) or OPL3 Bank 0 Write (0x5E)
        if cmd == 0x5A or cmd == 0x5E:
            reg, val = data[i+1], data[i+2]
            pending_writes.append((reg, val))
            i += 3
        elif cmd == 0x5F: # OPL3 Bank 1 (Ignore for OPL2 hardware)
            i += 3
        elif cmd == 0x61: # Wait N samples
            samples = struct.unpack('<H', data[i+1:i+3])[0]
            vsync_timer += (samples * TARGET_HZ / 44100.0)
            i += 3
            break_cluster = True
        elif cmd == 0x62: # Wait 735 (60Hz)
            vsync_timer += (735 * TARGET_HZ / 44100.0)
            i += 1
            break_cluster = True
        elif cmd == 0x63: # Wait 882 (50Hz)
            vsync_timer += (882 * TARGET_HZ / 44100.0)
            i += 1
            break_cluster = True
        elif 0x70 <= cmd <= 0x7F: # Wait n+1 samples
            vsync_timer += ((cmd & 0xF) + 1) * TARGET_HZ / 44100.0
            i += 1
            break_cluster = True
        elif cmd == 0x66: # End of Data
            break
        else:
            i += 1
            continue

        # If we hit a wait command, calculate the integer delta
        if vsync_timer > (last_vsync_int + 0.5) or i >= len(data):
            current_vsync_int = round(vsync_timer)
            delta = max(0, current_vsync_int - last_vsync_int)
            
            if pending_writes:
                for j, (r, v) in enumerate(pending_writes):
                    # Only the very last write in the group gets the delta
                    d = delta if j == len(pending_writes)-1 else 0
                    output.extend(struct.pack('<BBH', r, v, d))
                
                pending_writes = []
                last_vsync_int = current_vsync_int

    # End Sentinel
    output.extend(struct.pack('<BBH', 0xFF, 0, 0))
    
    with open(out_path, 'wb') as f:
        f.write(output)
    print(f"Exported {len(output)} bytes. Check if delays are now present in hexdump!")

if __name__ == "__main__":
    convert_vgm(sys.argv[1], sys.argv[2])