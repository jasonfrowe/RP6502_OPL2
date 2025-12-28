import struct
import sys
import gzip

# MUST match SONG_HZ in C
TARGET_HZ = 60 
BLOCK_SIZE = 512

def convert_vgm(vgm_path, out_path):
    with (gzip.open(vgm_path, 'rb') if vgm_path.endswith('.vgz') else open(vgm_path, 'rb')) as f:
        data = f.read()

    vgm_offset = struct.unpack('<I', data[0x34:0x38])[0] + 0x34
    output = bytearray()
    pending_writes = []
    vsync_timer = 0.0
    last_vsync_int = 0

    i = vgm_offset
    while i < len(data):
        cmd = data[i]
        if cmd == 0x5A or cmd == 0x5E:
            pending_writes.append((data[i+1], data[i+2]))
            i += 3
        elif cmd == 0x5F: i += 3
        elif cmd == 0x61:
            vsync_timer += (struct.unpack('<H', data[i+1:i+3])[0] * TARGET_HZ / 44100.0)
            i += 3
        elif cmd == 0x62: vsync_timer += (735 * TARGET_HZ / 44100.0); i += 1
        elif cmd == 0x63: vsync_timer += (882 * TARGET_HZ / 44100.0); i += 1
        elif 0x70 <= cmd <= 0x7F: vsync_timer += ((cmd & 0xF) + 1) * TARGET_HZ / 44100.0; i += 1
        elif cmd == 0x66: break
        else: i += 1; continue

        if vsync_timer > (last_vsync_int + 0.5) or i >= len(data):
            current_vsync_int = round(vsync_timer)
            delta = max(0, current_vsync_int - last_vsync_int)
            if pending_writes:
                # SECTOR ALIGNMENT CHECK:
                # If these writes won't fit in the current 512-byte block, 
                # pad the current block with NOPs (Reg 0) and move to next block.
                bytes_needed = len(pending_writes) * 4
                current_block_pos = len(output) % BLOCK_SIZE
                if (current_block_pos + bytes_needed) > BLOCK_SIZE:
                    padding_needed = BLOCK_SIZE - current_block_pos
                    output.extend(b'\x00' * padding_needed)

                for j, (r, v) in enumerate(pending_writes):
                    d = delta if j == len(pending_writes)-1 else 0
                    output.extend(struct.pack('<BBH', r, v, d))
                pending_writes, last_vsync_int = [], current_vsync_int

    # End of Song Sentinel (ensure it starts a new block)
    current_block_pos = len(output) % BLOCK_SIZE
    if current_block_pos > (BLOCK_SIZE - 4):
        output.extend(b'\x00' * (BLOCK_SIZE - current_block_pos))
    output.extend(struct.pack('<BBH', 0xFF, 0, 0))

    # Final Padding to full sector
    while len(output) % BLOCK_SIZE != 0:
        output.append(0)

    with open(out_path, 'wb') as f:
        f.write(output)
    print(f"Exported {len(output)} bytes (Sector Aligned).")

if __name__ == "__main__":
    convert_vgm(sys.argv[1], sys.argv[2])