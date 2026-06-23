# 🎛️ LuxSync DMX

**Real-time audio-reactive DMX lighting control plugin for DAWs + AI choreography engine**

[![Build & Release](https://github.com/Aragonlu/dmx-vst/workflows/Build%20&%20Release/badge.svg)](https://github.com/Aragonlu/dmx-vst/actions)
[![Latest Release](https://img.shields.io/github/v/release/Aragonlu/dmx-vst)](https://github.com/Aragonlu/dmx-vst/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## ✨ Features

### 🎚️ LuxSync DMX Plugin
- **VST3 & AU plugins** for professional DAWs (Ableton, Reaper, Logic Pro, FL Studio, etc.)
- **Real-time audio analysis** (FFT, onsets, energy bands, chroma detection)
- **DMX output**: Enttec USB Pro, ArtNet, sACN E1.31
- **Timeline editor**: Manual fixture control + keyframe system
- **DAW sync**: Follows project tempo and transport
- **Cross-platform**: Windows (VST3) + macOS (AU/VST3)

### 🎭 LuxSync AI Automator
- **Music playlist management** with offline analysis
- **AI choreography engine**: Auto-generates lighting sequences by musical section
- **Section detection**: Intro, Verse, Build, Drop, Chorus, Break, Outro
- **Motion figures**: Pan/Tilt synchronized to beat (sweep, mirror, circle, etc.)
- **2.5D stage visualizer** with real-time preview
- **Manual sequence editor** ("Creador") for custom coreographies
- **Live control**: Adjust colors, intensity, motion in real-time

---

## 🚀 Quick Start

### Installation

#### Windows
1. Download `LuxSync-DMX-VST3-v0.1.0-Windows-x64.exe` from [Releases](https://github.com/Aragonlu/dmx-vst/releases)
2. Run the installer
3. Select VST3 folder (default: `C:\Program Files\Common Files\VST3`)
4. Restart your DAW
5. Plugin appears under "Aragon → LuxSync DMX"

#### macOS
1. Download the appropriate DMG from [Releases](https://github.com/Aragonlu/dmx-vst/releases):
   - **AU Plugin**: `LuxSync-DMX-AU-v0.1.0-macOS-arm64.dmg` (or x86_64)
   - **VST3 Plugin**: `LuxSync-DMX-VST3-v0.1.0-macOS-arm64.dmg` (or x86_64)
   - **Standalone**: `LuxSync-AIAutomator-v0.1.0-macOS-arm64.dmg`
2. Open the DMG and drag plugin/app to suggested location
3. Restart your DAW
4. Plugin appears in the list

#### Standalone Automator (All platforms)
Download and extract `LuxSync-AIAutomator-vX.X.X-[Windows|macOS]-*.zip`

---

## 📋 Release Artifacts

| Platform | Type | File | Use Case |
|---|---|---|---|
| **Windows** | VST3 Plugin | `LuxSync-DMX-VST3-v*.exe` | Studio DAW |
| **Windows** | Standalone | `LuxSync-AIAutomator-v*-Windows.zip` | DJ events, live shows |
| **macOS** | AU Plugin | `LuxSync-DMX-AU-v*.dmg` | Logic Pro, GarageBand |
| **macOS** | VST3 Plugin | `LuxSync-DMX-VST3-v*.dmg` | Cross-DAW plugin |
| **macOS** | Standalone | `LuxSync-AIAutomator-v*.dmg` | DJ events, live shows |

All installers include:
- ✅ Automatic plugin registration
- ✅ VST3/AU folder detection
- ✅ Cross-platform compatibility (Windows & macOS)
- ✅ 64-bit only (32-bit deprecated)

---

## 🔧 Development

### Requirements
- **CMake** >= 3.22
- **Visual Studio 2022** (Windows) or **Xcode** (macOS)
- **C++17** standard
- JUCE 8.0.4 (auto-fetched via FetchContent)

### Build from Source

#### Windows
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DmxVst
cmake --build build --config Release --target LuxAutomator
```

#### macOS
```bash
# Apple Silicon (arm64)
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
# Or Intel (x86_64)
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64

cmake --build build --config Release --target DmxVst
cmake --build build --config Release --target LuxAutomator
```

**Outputs:**
- Plugin: `build/DmxVst_artefacts/Release/`
- Automator: `build/LuxAutomator_artefacts/Release/`

### Packaging & Release

See [RELEASE_GUIDE.md](RELEASE_GUIDE.md) for detailed instructions on:
- Local packaging with NSIS (Windows) and DMG (macOS)
- GitHub Actions CI/CD setup
- Creating automated releases

---

## 📚 Documentation

- **[PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md)** — Detailed project architecture & features
- **[RELEASE_GUIDE.md](RELEASE_GUIDE.md)** — Build, package, and release guide
- **[DEVLOG.md](DEVLOG.md)** — Development history and feature roadmap

---

## 🎮 Quick Tutorial

### Using the VST3 Plugin in Your DAW

1. **Load Plugin**: Add LuxSync DMX as an audio effect on any track
2. **Analyze**: Select audio file or enable live analysis
3. **Configure Output**: Select DMX interface (Enttec/ArtNet/sACN)
4. **Map Fixtures**: Add your lighting fixtures in the "Fixtures" tab
5. **Timeline**: Build custom sequences or use auto-generated patterns
6. **Live Control**: Adjust colors, intensity, motion with the UI sliders

### Using the Automator Standalone

1. **Load Music**: Drag & drop songs or select from folder
2. **Wait for Analysis**: AI analyzes structure, energy, chroma
3. **Preview**: Watch 2.5D stage visualization in real-time
4. **Customize**: Adjust motion, colors per section, create manual sequences
5. **Output**: DMX to your lighting rig via Enttec/ArtNet/sACN
6. **Save**: Session and coreographies persist for future use

---

## 🛠️ Roadmap

- [x] VST3 plugin for Windows + macOS
- [x] AI choreography engine (auto-generate sequences)
- [x] Section detection (Intro/Verse/Build/Drop/Chorus/Break/Outro)
- [x] Motion figures (Pan/Tilt sync)
- [x] Stage visualizer (2D + 2.5D perspective)
- [x] Manual sequence editor
- [ ] MIDI control input
- [ ] Preset library (colors, motions, sequences)
- [ ] Cloud sync & collaboration
- [ ] Plugin for Ableton Live theme integration
- [ ] OpenGL 3D preview (stretch goal)

---

## 📝 License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for details.

---

## 👥 Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 💬 Support

- **Issues**: [GitHub Issues](https://github.com/Aragonlu/dmx-vst/issues)
- **Discussions**: [GitHub Discussions](https://github.com/Aragonlu/dmx-vst/discussions)
- **Email**: contact@luxsync.local

---

## 🎵 Acknowledgments

- Built with **JUCE 8.0.4** — The #1 C++ framework for audio plugins
- DMX protocols: **Enttec Pro**, **ArtNet**, **sACN E1.31**
- AI engine inspired by modern music analysis techniques

---

**LuxSync DMX v0.1.0** — *Where music meets light* ✨🎛️

[⬆ Back to top](#-luxsync-dmx)
