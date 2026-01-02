# Remus - Transport-Aligned Looper

A simple LV2 plugin for recording and looping audio samples that strictly align to transport position.

## Features

- **Transport-aligned recording**: Records loops that sync perfectly with your DAW's tempo
- **Automatic tempo sync**: Uses transport BPM and time signature automatically
- **Bar-boundary recording**: Recording starts at the next bar when enabled
- **Persistent loops**: Saves recorded loops with your DAW project (optional)
- **Mono audio input/output**: Simple, focused design
- **Configurable loop length**: Set loop length in bars (1-64)
- **Low latency**: Designed for real-time performance

## Parameters

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| Audio In | Audio Input | - | - | Mono audio input |
| Audio Out | Audio Output | - | - | Mono audio output (recorded loop or silence) |
| Control | Atom Input | - | - | Transport position information |
| Record Enable | Control | 0-1 (toggle) | 0 | Arm recording on transition to zero (waits for bar boundary) |
| Loop Length | Control | 1-64 bars | 4 | Loop length in bars |
| Persist Loop | Control | 0-1 (toggle) | 1 | Save loop with project |

## How It Works

1. **Transport sync**: The plugin automatically reads BPM and time signature from your DAW
2. **Enable recording**: Toggle "Record Enable" control
3. **Wait for bar**: Plugin waits for the next bar boundary to start recording
4. **Automatic loop**: Recording stops when the configured loop length is reached
5. **Playback**: The recorded loop plays back continuously, aligned to transport
6. **Persistence**: If enabled, the loop is saved with your DAW project and restored on load
7. **Re-record**: Toggle "Record Enable" again to record a new loop

The loop length is calculated from transport: `beats_per_bar (from transport) × loop_length (bars) × 60 / BPM (from transport) × sample_rate`

## Building

### Requirements

- gcc or compatible C compiler
- LV2 development headers
- make
- pkg-config

### Build Instructions

```bash
# Build the plugin
make

# Install system-wide (requires sudo)
sudo make install

# Install to user directory (~/.lv2)
make install-user
```

### Clean

```bash
make clean
```

## Installation

After building, the plugin will be installed to your LV2 plugin directory:
- System-wide: `${PREFIX}/lib/lv2/remus.lv2/` (usually `/usr/local/lib/lv2/`)
- User install: `~/.lv2/remus.lv2/`

Your LV2 host should automatically detect the plugin after installation.

## Project Structure

```
remus/
├── src/              # C source code
│   └── remus.c
├── plugins/          # Plugin bundles
│   └── remus.lv2/
│       ├── manifest.ttl
│       └── remus.ttl
├── build/            # Build artifacts (generated)
├── Makefile          # Build system
├── README.md
└── LICENSE
```

## Usage Examples

### Basic 4-bar loop (4/4 time at 120 BPM)
- Loop Length: 4 bars
- Result: 8-second loop (automatically synced to transport)

### Short 1-bar loop (any time signature)
- Loop Length: 1 bar
- Adapts to your DAW's time signature and tempo

### Long ambient loop
- Loop Length: 16 bars
- Perfect for creating evolving textures

### Persistence
- **Enabled** (default): Your loop is saved with the DAW project and restored when you reopen
- **Disabled**: Loop is lost when closing the DAW (useful for temporary sketching)

## Technical Details

- Maximum buffer size: 10 minutes at 48kHz
- Hard real-time capable
- Uses LV2 state extension for persistence
- Reads tempo and time signature from transport
- Waits for bar boundaries before recording
- No external dependencies beyond LV2 headers

## License

ISC License

## Author

Built for transport-aligned creative looping workflows.
