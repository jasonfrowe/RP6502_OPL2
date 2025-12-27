# RP6502-OPL2 Jukebox
### FPGA-Accelerated FM Synthesis for the Picocomputer

This project implements a cycle-accurate Yamaha YM3812 (OPL2) sound system for the RP6502 Picocomputer. It utilizes a TinyFPGA BX as a high-speed PIX bus sniffer and hardware command buffer, allowing the 65C02 to stream complex FM music (like Doom or Space Quest III) with zero CPU-side wait states.

## 🚀 Key Features
- **FPGA-Side FIFO:** A 512-entry hardware buffer handles the OPL2's strict timing requirements (3.3µs and 23µs recovery times), allowing the 6502 to dump register writes at full 8MHz PIX bus speed.
- **Dynamic Voice Management:** The Python toolchain handles polyphony and Least-Recently Used (LRU) voice allocation, allowing MIDI files with up to 16 channels to be played across the OPL2's 9 physical voices.
- **High-Resolution Timing:** Supports 120Hz/140Hz playback via a fractional VSync accumulator, ensuring perfect rhythmic accuracy for classic DOS game music.
- **Pre-Baked Frequency Math:** All frequency (F-Number/Block) and OPL2 operator math is offloaded to the Python converter, making the 6502 sequencer extremely lightweight.

## 🛠 Hardware Architecture

### The PIX Interface
The TinyFPGA BX sniffs the RP6502 PIX bus using Double Data Rate (DDR) sampling.
- **PHI2 (Clock):** Drives the synchronous sniffer logic.
- **PIX[0:3] (Data):** Transmits 32-bit frames over 4 clock cycles.
- **Address Mapping:** OPL2 registers are mapped to XRAM `$FF00` (Index) and `$FF01` (Data), with a hardware FIFO flush register at `$FF02`.

### Audio Chain
1. **JTOPL2 Core:** A cycle-accurate OPL2 implementation by Jotego.
2. **Sigma-Delta DAC:** A 16-bit PDM DAC running at 16MHz.
3. **External Filter:** A simple RC low-pass filter (1kΩ / 0.1µF) connected to Pin 14.

## 🔌 Pin Mapping (TinyFPGA BX)
| Function | FPGA Pin | RP6502 Pin |
| :--- | :--- | :--- |
| PHI2 (Bus Clock) | A2 | Pin 1 |
| PIX[0] | A1 | Pin 2 |
| PIX[1] | B1 | Pin 3 |
| PIX[2] | C2 | Pin 4 |
| PIX[3] | C1 | Pin 5 |
| AUDIO_OUT | H9 | Pin 14 |
| LED | B3 | Onboard |

## 📂 Software Pipeline

### 1. Music Creation
Use **Furnace Tracker** or any MIDI sequencer. 
- For Furnace: Set System to `YM3812`, Clock to `4,000,000Hz`, and Rate to `60Hz` or `120Hz`.
- Export as standard MIDI.

### 2. Conversion (`midi2pix.py`)
The Python script converts MIDI files into a optimized 6-byte binary format:
`[Type][Channel][Data1][Data2][Delay_After (16-bit)]`

**Features:**
- **Lazy Patching:** Only sends instrument operator writes when an OPL channel actually switches instruments.
- **Drum Mapping:** Automatically maps MIDI Channel 10 to OPL channels 6-8 and forces correct percussion pitches.
- **Redundancy Filter:** Removes unnecessary register writes to maximize bus bandwidth.

```bash
python3 tools/midi2pix.py music/e1m1.mid src/doom.bin
```

### 3. The 6502 Engine (LLVM-MOS)
The C engine reads the `.bin` data from XRAM. Because the frequency math and voice allocation are pre-calculated, the `update_song()` function is a simple, high-speed "byte copier."

## 🛠 Build & Run
1. **Flash FPGA:** Use `apio` or `tinyprog` to flash the `top.bin` to the TinyFPGA BX.
2. **Compile 6502 Code:** Use the RP6502 SDK (CMake/LLVM-MOS) to build the executable.
3. **Upload:**
   ```bash
   rp6502.py RP6502_OPL2.rp6502 -a 0x10000:src/doom.bin
   ```

## 🎹 OPL2 Register Cheat Sheet
- **$FF00:** Write Register Index (0-255).
- **$FF01:** Write Data value.
- **$FF02:** Write `0xAA` to hardware-flush the FIFO.

## 📜 Credits
- **JTOPL2 Core:** Developed by Jose Tejada (Jotego).
- **RP6502 Hardware:** Developed by Lawrence Manning.
- **Sequencer Logic:** Developed by Jason Rowe

***

### 💡 Pro-Tip for Doom E1M1:
If the music feels "heavy," ensure `SONG_HZ` in `main.c` is set to **120** or **140** and that the Python script was run with a matching `VSYNC_RATE`. This activates the high-resolution rhythm logic.
