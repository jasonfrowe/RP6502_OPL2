#!/usr/bin/env python3
import struct
import sys
import gzip
import os

# --- CONFIGURATION ---
# TARGET_HZ must match SONG_HZ in your main.c
TARGET_HZ = 60 

def convert_vgm(vgm_path, out_path):
    # Handle compressed vgz files
    if vgm_path.endswith('.vgz'):
        with gzip.open(vgm_path, 'rb') as f:
            data = f.read()
    else:
        with open(vgm_path, 'rb') as f:
            data = f.read()

    if data[:4] != b'Vgm ':
        print("Error: Not a valid VGM file")
        return

    # Find data offset
    vgm_offset = struct.unpack('<I', data[0x34:0x38])[0] + 0x34
    
    output = bytearray()
    pending_writes = []
    
    # Timing accumulator to handle sub-frame delays
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
            continue # Don't process timing until a wait cmd
        
        elif cmd == 0x5F: # OPL3 Bank 1 (Ignore for OPL2 hardware)
            i += 3
            continue

        elif cmd == 0x61: # Wait N samples
            samples = struct.unpack('<H', data[i+1:i+3])[0]
            vsync_timer += (samples * TARGET_HZ / 44100.0)
            i += 3
        elif cmd == 0x62: # Wait 735 (60Hz)
            vsync_timer += (735 * TARGET_HZ / 44100.0)
            i += 1
        elif cmd == 0x63: # Wait 882 (50Hz)
            vsync_timer += (882 * TARGET_HZ / 44100.0)
            i += 1
        elif 0x70 <= cmd <= 0x7F: # Wait n+1 samples
            vsync_timer += ((cmd & 0xF) + 1) * TARGET_HZ / 44100.0
            i += 1
        elif cmd == 0x66: # End of Data
            break
        else:
            i += 1
            continue

        # If the accumulated time is enough to cross a VSync integer boundary...
        current_vsync_int = round(vsync_timer)
        delta = current_vsync_int - last_vsync_int

        if delta > 0 or pending_writes:
            if pending_writes:
                for j, (r, v) in enumerate(pending_writes):
                    # Only the very last write in the cluster carries the wait
                    d = delta if j == len(pending_writes)-1 else 0
                    output.extend(struct.pack('<BBH', r, v, d))
                
                pending_writes = []
                last_vsync_int = current_vsync_int
            elif delta > 0:
                # If there was a long wait with no writes, insert a No-Op wait
                # We use register 0x00 (unused) as a dummy wait
                output.extend(struct.pack('<BBH', 0x00, 0, delta))
                last_vsync_int = current_vsync_int

    # End Sentinel (0xFF 00 00 00)
    output.extend(struct.pack('<BBH', 0xFF, 0xFF, 0))

    # ADD PADDING (16 bytes of zeros)
    # This prevents the RIA from hitting physical EOF exactly on the sentinel,
    # making the logic much smoother.
    output.extend(b'\x00' * 16)
    
    with open(out_path, 'wb') as f:
        f.write(output)
    
    print(f"File: {vgm_path}")
    print(f"Output: {out_path} ({len(output)} bytes)")
    print(f"Rate: {TARGET_HZ} Hz")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python vgm2pix.py input.vgm output.bin")
    else:
        convert_vgm(sys.argv[1], sys.argv[2])