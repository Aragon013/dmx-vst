# LuxSync DMX — Visión General del Proyecto

> Documento generado el 2026-06-12 a partir del código fuente real.

---

## ¿Qué es esto?

**LuxSync DMX** es un plugin VST3/AU para DAW (y una app Standalone) escrito en
**C++17 con JUCE 8.0.4**. Su objetivo es analizar audio y controlar luces DMX en
tiempo real, sincronizadas con el transporte del DAW o con la reproducción interna.

Hay **dos productos** en el mismo repositorio:

| Producto | Tipo | Propósito |
|---|---|---|
| `LuxSync DMX` (plugin) | VST3 + Standalone | Análisis offline/live, timeline de fixtures, salida DMX desde DAW |
| `LuxSync AI Automator` | Standalone GUI | App para DJs/eventos: playlist + coreografía IA + visualizador escenario |

---

## Entorno de build

- **Compilador:** MSVC (Visual Studio 2022). JUCE 8 eliminó soporte MinGW.
- **CMake portable:** `C:\Users\Aragonlu\tools\cmake` (v3.30.5), en PATH.
- **Configurar:** `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- **Compilar:** `cmake --build build --config Release`
- **macOS:** genera también AU (el CMakeLists lo detecta automáticamente).

Outputs en `build/DmxVst_artefacts/Release/` (plugin) y
`build/LuxAutomator_artefacts/Release/` (Automator).

> **OJO:** al añadir archivos `.cpp` nuevos hay que re-ejecutar CMake (`cmake -S .
> -B build ...`) antes de compilar. Si el Standalone está abierto, el link falla
> con LNK1104 (archivo bloqueado) — cerrar la app primero.

---

## Estructura del repositorio

```
dmx-vst/
├── CMakeLists.txt          — define DmxVst (plugin) y LuxAutomator (app)
├── source/                 — código del plugin VST3/Standalone
│   ├── PluginProcessor.*   — núcleo: audio, DMX, transporte, estado
│   ├── PluginEditor.*      — ventana redimensionable 980×700, tabs + header
│   ├── AudioAnalyzer.*     — análisis offline (FFT order 10, onsets)
│   ├── LiveAnalyzer.h      — análisis en vivo (RMS + graves + transitorios)
│   ├── AudioPanel.*        — tab "Análisis": waveform + 8 knobs de detección
│   ├── WaveformView.*      — dibuja forma de onda + líneas de onset
│   ├── FixtureModel.*      — Fixture, ChannelDef, serialización ValueTree
│   ├── FixturesPanel.*     — tab "Equipos": ListBox + editor overlay
│   ├── FixtureEditor.*     — overlay de edición de fixture (no ventana nativa)
│   ├── ChannelRow.*        — fila editable de canal en el editor
│   ├── Sequence.h          — Keyframe, EffectClip, interpolación (header-only)
│   ├── TimelinePanel.*     — tab "Timeline": piano-roll por canal
│   ├── OutputPanel.*       — tab "Salida DMX": medidores + transporte
│   ├── ReactivePanel.*     — tab "Reactivo": reglas señal→luz en vivo
│   ├── ConnectorPanel.*    — vista modo Connector (knobs LiveAnalyzer)
│   ├── Reactive.h          — struct ReactiveRule + paleta (header-only)
│   ├── SharedHub.h         — comunicación entre instancias mismo proceso (header-only)
│   ├── ArtNetSender.*      — salida DMX por Art-Net (UDP 6454)
│   ├── SacnSender.*        — salida DMX por sACN E1.31 (multicast UDP 5568)
│   ├── EnttecSender.*      — salida DMX por Enttec USB Pro (serie FTDI VCP)
│   ├── SerialPort.*        — wrapper de puerto serie (Windows + macOS)
│   ├── KnobLookAndFeel.h   — LookAndFeel knobs circulares (header-only)
│   └── LuxLookAndFeel.h    — tema global "Estudio a Oscuras" (header-only)
└── apps/automator/         — código del AI Automator
    ├── Main.cpp
    ├── AutomatorComponent.*  — componente principal: playlist + transporte + stage
    ├── PlaylistManager.h     — gestión de temas, análisis background, shows
    ├── AudioPlayer.h         — AudioDeviceManager + AudioTransportSource
    ├── PlaybackEngine.h      — tick DMX + emisión Art-Net a ~44 Hz
    ├── ChoreographyEngine.h  — generación de shows DMX desde análisis IA
    ├── DefaultRig.h          — rig por defecto (4 PAR + 2 wash + barra + cabezas + spiders + strobes)
    ├── StageVisualizer.h     — visualizador 2D del escenario (drag fixtures)
    ├── DmxPreview.h          — medidores DMX simples (vista previa rápida)
    ├── DmxShow.h             — estructura de datos de un show pre-calculado
    ├── Track.h               — Track (archivo + estado + análisis + show)
    ├── TrackAnalysis.h       — resultados del análisis de un tema
    ├── PreprocessWorker.h    — hilo background de análisis + separación de stems
    ├── SidecarStore.h        — caché sidecar (.lux) para no re-analizar
    ├── StemSeparator.h       — llamada a HTDemucs/Python para separar stems
    ├── OfflineAnalyzer.h     — wrapper análisis batch
    ├── DemucsProcessProvider.h — proveedor backend Demucs
    └── PlaybackEngine.h      — motor de playback (reutilizado)
```

---

## El Plugin VST3/Standalone — `LuxSync DMX`

### Arquitectura general

```
DAW / Standalone
      │  audio
      ▼
PluginProcessor (AudioProcessor + HighResolutionTimer + Timer)
      ├─ processBlock()   →  LiveAnalyzer.process()  (nivel/graves/transitorios)
      │                       └→ SharedHub publica señales si rol=Connector
      ├─ hiResTimerCallback() → transporte interno (si Standalone o sin DAW)
      ├─ renderDmxFrame(beats) → evalúa keyframes + EffectClips + ReactiveRules
      │                           └→ dmxBuffer[8 universos × 512 canales]
      └─ sendDmxToNetwork() → ArtNetSender + SacnSender + EnttecSender

PluginEditor (980×700, redimensionable 820×560 .. 2600×1700)
      ├─ Header 54px  — "LuxSync DMX" + estado transporte
      ├─ Franja rol   — Main / Connector + nombre bus
      └─ TabbedComponent (tabs 38px)
           ├─ "Análisis"   → AudioPanel   (waveform + 8 knobs)
           ├─ "Equipos"    → FixturesPanel (ListBox + FixtureEditor overlay)
           ├─ "Timeline"   → TimelinePanel (piano-roll)
           ├─ "Salida DMX" → OutputPanel   (medidores + transporte)
           └─ "Reactivo"   → ReactivePanel (medidores live + reglas)
           [modo Connector muestra ConnectorPanel en vez de los tabs]
```

---

### Transporte unificado

El procesador tiene **dos fuentes de tiempo** que se resuelven en `getPlayheadBeats()`:

1. **Host DAW** — se leen las `AudioPlayHead::PositionInfo` en `processBlock`.
   En modo Standalone se ignora (el wrapper Standalone provee un playhead "muerto").
2. **Transporte interno** — avanza en `HighResolutionTimer::hiResTimerCallback()`
   a ~60 Hz. Se controla con `transportPlay/Stop/Toggle/Rewind`,
   `setInternalBpm/Length/Loop`.

Si hay un archivo de audio cargado (`hasAudioFile`), el tiempo sigue a
`AudioTransportSource::getCurrentPosition()` convertido a beats.

---

### Análisis offline (`AudioAnalyzer`)

- Carga el archivo con `AudioFormatManager`, suma a mono.
- FFT orden 10 (1024 muestras), hop 512.
- Guarda `bandMags` log-espaciadas (64 bandas) por frame y `flux` por frame.
- `recomputeOnsets(result, params)` recalcula onsets instantáneamente al mover
  un knob (sin releer el archivo).

**8 parámetros ajustables (`OnsetParams`):**
`delta`, `threshold`, `neighbourhood`, `minSpacingSec`, `lowCut`, `highCut`,
`smoothing`, `gain`.

---

### Fixtures y timeline

**`FixtureModel`:**  
`Fixture` → `std::vector<ChannelDef>` → cada canal tiene `keyframes` +
`clips` (EffectClip LFO) + `colour` (color de la pista) + `defaultValue`.

**`Sequence.h`:**  
- `Keyframe {timeBeats, value 0-255, stepped}` — linear o step.
- `EffectClip {startBeats, lengthBeats, periodBeats, low, high, type}` — LFO
  (Sine/Triangle/SawUp/SawDown/Square/Random) encima de los keyframes.
- `interpolateKeyframes()` + `evaluateEffectClip()` — usados en `renderDmxFrame`.

**`TimelinePanel`:**  
Piano-roll por canal. Zoom/scroll horizontal (Ctrl+rueda). Colores por canal
(swatch clicable → `ColourSelector`). Playhead del DAW (verde si playing).
Scrubbing en la regla. Botones Play/Stop/Rewind/Loop + SPACE/ENTER.

---

### Salidas DMX

| Protocolo | Clase | Transporte |
|---|---|---|
| Art-Net | `ArtNetSender` | UDP 6454 (broadcast o unicast) |
| sACN E1.31 | `SacnSender` | UDP multicast `239.255.x.x:5568` |
| Enttec USB Pro | `EnttecSender` + `SerialPort` | Serie FTDI VCP (sin driver especial) |

El buffer DMX vive en el processor: `std::array<std::atomic<uint8_t>, 8×512>`.
`renderDmxFrame` lo rellena y `sendDmxToNetwork` lo emite, ambos llamados
desde el timer del `OutputPanel` a ~44 Hz.

> **Pendiente:** mover el tick de renderizado al `HighResolutionTimer` del processor
> para que funcione sin la ventana abierta (modo headless/producción).

---

### Modo Main / Connector (`SharedHub`)

Permite usar **varias instancias del plugin en el mismo DAW** (mismo proceso):

- **Connector**: se inserta en cada pista stem (Bajo, Batería…). Publica
  `BusSignals {level, bass, transient, transientCount}` en `SharedHub`
  bajo un nombre libre.
- **Main**: tiene el rig y las reglas reactivas. Cada `ReactiveRule` puede
  apuntar a un bus (nombre de pista) en vez de al audio propio.

`SharedHub` usa `weak_ptr`: cuando un Connector se destruye, su bus desaparece
automáticamente. Solo funciona en el mismo proceso (limitación: sandbox/bridge).

---

### UI — LuxLookAndFeel ("Estudio a Oscuras")

Paleta de colores (`LuxLookAndFeel::Palette`):

| Constante | Valor | Uso |
|---|---|---|
| `bg0` | `0xff0a0c10` | fondo más oscuro |
| `bg1` | `0xff10131a` | fondo principal |
| `surface` | `0xff181d27` | superficies elevadas |
| `accent` | `0xffffb020` | ámbar — color vivo principal |
| `accent2` | `0xff4fc3f7` | cian — curvas de timeline |
| `textHi` | `0xffe9edf4` | texto principal |

Knobs: circulares siempre (cuadrado centrado `jmin(w,h)`), cuerpo plano
`0xff181c24`, arco de valor con glow ámbar 2 pasadas.

---

## El AI Automator — `LuxSync AI Automator`

App Standalone para DJs y eventos. Flujo:

```
PlaylistManager
  ├─ añadir archivos → PreprocessWorker (hilo bg)
  │     ├─ SidecarStore (.lux cache) — si ya analizado, carga rápido
  │     └─ AudioAnalyzer → opcionalmente StemSeparator (HTDemucs/Python)
  │           → TrackAnalysis {bpm, onsets, energy, stems: drums/bass/vocals/other}
  └─ ChoreographyEngine::generate() → DmxShow (show pre-calculado en beats)

AutomatorComponent (UI principal)
  ├─ ListBox de playlist
  ├─ Transporte Play/Stop + slider de posición
  ├─ DmxPreview o StageVisualizer (toggle "Escenario")
  ├─ ComboBox "Estilo" (7 presets)
  ├─ Toggle "Stems IA" (activa separación por instrumento)
  └─ Botón "Audio..." → AudioDeviceSelectorComponent

PlaybackEngine
  └─ tick(seconds) a ~44 Hz → renderFrame del DmxShow → Art-Net UDP
```

### ChoreographyEngine

Genera `DmxShow` a partir de `TrackAnalysis` + rig de fixtures.

**Sin stems:** mapeo mono sobre mezcla completa.

**Con stems (HTDemucs):**
- Batería → PAR/strobe (flashes en transitorios)
- Bajo → barra LED / wash (energía de graves)
- Voces → acentos en cabezas móviles
- Melodía/otros → movimiento Pan/Tilt de cabezas

**7 estilos de show:**

| Estilo | Color | Movimiento |
|---|---|---|
| Equilibrado | Cycle | Unison |
| Chase | Cycle | Chase |
| Alterno | Warm | Alternate |
| Onda | Cool | Wave |
| Arcoiris | Rainbow | Unison |
| Pulso | Cycle | Pulse |
| Mono Chase | Mono | Chase |

### StageVisualizer

Visualizador 2D del escenario con drag & drop de fixtures (posición persistida en
`%APPDATA%/LuxSync/stage_layout.xml`). Clic derecho → asignar stem a un fixture.
Doble clic → volver a posición automática.

Tipos de fixture reconocidos por nombre/modelo: `Par`, `Wash`, `Bar`, `MovingHead`,
`Spider`, `Strobe`, `Generic`.

### Rig por defecto (`DefaultRig`)

13 fixtures, universo 0, direcciones consecutivas:
- 4× PAR RGBW+Dimmer (5 ch)
- 2× Wash RGBW+Dimmer (5 ch)
- 1× Barra LED RGB (3 ch)
- 2× Cabeza móvil Pan/Tilt/Dimmer/RGB (6 ch)
- 2× Spider Pan/Tilt/Dimmer/RGB (6 ch)
- 2× Strobe Dimmer/Strobe (2 ch)

---

## Persistencia

| Qué | Dónde | Cómo |
|---|---|---|
| Fixtures + keyframes + clips + reglas reactivas | Proyecto DAW | `getStateInformation` / `setStateInformation` (ValueTree → MemoryBlock) |
| Dispositivo de audio (Automator) | `%APPDATA%/LuxSync/audio_device.xml` | `deviceManager.createStateXml()` |
| Layout del escenario (Automator) | `%APPDATA%/LuxSync/stage_layout.xml` | XML manual |
| Sesión del Automator (playlist, style, stemAssign) | `%APPDATA%/LuxSync/automator_session.xml` | XML manual |
| Caché de análisis | `.lux` sidecar junto al audio | `SidecarStore` |

---

## Estado actual del build

| Componente | Estado |
|---|---|
| Plugin VST3 | ✅ Build Release OK, corre en DAW y Standalone |
| LuxAutomator | ✅ Build Release OK |
| Análisis offline + onsets + knobs | ✅ Hecho |
| Timeline piano-roll + zoom/scroll | ✅ Hecho |
| EffectClips (LFO) | ✅ Hecho (modelo) |
| Análisis en vivo + Reactivo | ✅ Hecho |
| Modo Main/Connector + SharedHub | ✅ Hecho |
| UI LuxLookAndFeel completa | ✅ Hecho |
| Transporte interno + reproducción audio | ✅ Hecho |
| Art-Net + sACN + Enttec (código) | ✅ Código presente |
| Automator: playlist + análisis bg | ✅ Hecho |
| Automator: coreografía IA + estilos | ✅ Hecho |
| Automator: StageVisualizer + drag | ✅ Hecho |
| Automator: asignación stem→fixture | ✅ Hecho |
| Automator: selección dispositivo audio | ✅ Hecho |
| **Tick DMX en HighResolutionTimer** | ⏳ Pendiente (hoy depende del timer de OutputPanel) |
| **Timeline: clips de efecto en UI** | ⏳ Pendiente (el modelo existe, falta UI de edición) |
| **Salida Enttec validada en hardware** | ⏳ Pendiente |

---

## Comandos rápidos

```powershell
# Asegurarse de que CMake está en PATH (si terminal nueva)
$env:Path = "$env:USERPROFILE\tools\cmake\bin;$env:Path"

# Configurar (solo si añadiste archivos .cpp o cambió CMakeLists.txt)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Compilar
cmake --build build --config Release

# El plugin VST3 queda en:
#   build/DmxVst_artefacts/Release/VST3/LuxSync DMX.vst3
# El Standalone en:
#   build/DmxVst_artefacts/Release/Standalone/LuxSync DMX.exe
# El Automator en:
#   build/LuxAutomator_artefacts/Release/LuxSync AI Automator.exe
```
