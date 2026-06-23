# LuxSync DMX — Registro de desarrollo (DEVLOG)

> Registro vivo del proyecto. Doble propósito:
> 1. **Recuperar contexto** rápidamente si se pierde el chat de la IA.
> 2. Servir de **base para el documento público** (release notes / manual) cuando haya una versión liberable.
>
> Convención: cada sesión añade una entrada en **Bitácora de sesiones** (arriba la más reciente).
> El **State Ledger** de abajo refleja siempre el estado ACTUAL (se sobrescribe).

---

## Identidad del producto

- **Nombre:** LuxSync DMX
- **Qué es:** Plugin **VST3 / AU** que analiza audio y controla luces **DMX**, sincronizado con el transporte del DAW. También corre como **Standalone**.
- **Ubicación:** `C:\Users\Aragonlu\dmx-vst` (proyecto independiente; NO se refactoriza ni se renombra).
- **Stack:** JUCE 8.0.4 (FetchContent) + CMake, C++17, compilador **MSVC** (JUCE 8 eliminó soporte MinGW/GCC).
- **Targets:** Windows → VST3 + Standalone. macOS → VST3 + AU + Standalone.

### Build (Windows / MSVC)
```powershell
# Si CMake portable no está en PATH en una terminal nueva:
$env:Path = "$env:USERPROFILE\tools\cmake\bin;$env:Path"

# Configurar (reconfigurar SIEMPRE que se añadan archivos nuevos a CMake):
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Compilar:
cmake --build build --config Release   # o Debug
```
Artefactos: `build/DmxVst_artefacts/Release/` (VST3 + Standalone).
> OJO: si el Standalone `.exe` está abierto, el link del Standalone falla (LNK1104, archivo bloqueado). El VST3 igual se genera. Cerrar la app antes de recompilar el Standalone.

---

## State Ledger (estado ACTUAL)

### ✅ Hecho
- **UI:** editor redimensionable con tema "Estudio a Oscuras" (`LuxLookAndFeel`, `KnobLookAndFeel`), knobs premium, 5 pestañas:
  **Análisis · Equipos · Timeline · Salida DMX · Reactivo** + toggle de rol **Main / Connector**.
- **Análisis de audio:**
  - Offline (`AudioAnalyzer`): carga archivo, waveform, detección de onsets (spectral flux FFT) con 8 knobs de afinado, recálculo instantáneo.
  - En vivo (`LiveAnalyzer`, en `processBlock`, sin FFT): nivel, graves, transitorios.
- **Equipos / Fixtures** (`FixtureModel`, `FixtureEditor`, `ChannelRow`): editor completo, canales por tipo, plantillas rápidas, patch DMX (universo/dirección/cantidad), serialización en `ValueTree`.
- **Timeline / piano-roll** (`TimelinePanel`, `Sequence`): keyframes por canal (rampa/step), grid en compases/beats, guías de onset, zoom + scroll horizontal, color por canal, playhead.
- **Clips de efecto (LFO) en el timeline** (modelo mixto): `EffectClip` en `Sequence.h` (Sine/Triangle/SawUp/SawDown/Square/Random; `startBeats`, `lengthBeats`, `periodBeats`, `low`/`high`, `phase`). `evaluateChannel()` combina clips + keyframes (el clip activo SOBRESCRIBE). `ChannelDef` lleva `std::vector<EffectClip> clips` (serializado en `ValueTree`). UI en `TimelinePanel`: toggle **Efectos** → arrastrar crea clip, arrastrar bordes redimensiona, arrastrar cuerpo mueve, doble clic edita (popup tipo/periodo/min/max/fase), clic derecho borra; se dibujan como bandas translúcidas con la forma de onda muestreada dentro.
- **Reproducción de audio (Standalone):** `AudioTransportSource`, transporte interno por muestras + `HighResolutionTimer`, scrub en la regla, loop.
- **Salida DMX (buffer):** `dmxBuffer` (8 universos × 512), `renderDmxFrame(beats)`, visualización por canal en `OutputPanel`.
- **Render/envío DMX en el processor:** el processor tiene su propio `juce::Timer` (hilo de mensajes, ~44 Hz) que hace `renderDmxFrame + sendDmxToNetwork` siempre, **aunque la ventana del editor esté cerrada**. Mismo hilo que las ediciones de fixtures/keyframes → sin data races. (`OutputPanel` ya solo lee el buffer para mostrarlo.)
- **Motor reactivo** (`Reactive`, `ReactivePanel`, `SharedHub`, `ConnectorPanel`): reglas señal→canal o señal→color RGB; buses con nombre entre instancias del mismo proceso (modo Connector publica, Main consume).
- **Persistencia:** fixtures + reglas + rol + busName en `get/setStateInformation` (viaja con el proyecto del DAW).
- **Salida DMX por red — Art-Net (UDP 6454):** `ArtNetSender` (paquete ArtDmx, broadcast o unicast, secuencia por universo). Integrado en el processor (`sendDmxToNetwork()` tras cada `renderDmxFrame`, emite solo los universos usados por fixtures). UI en `OutputPanel`: toggle Art-Net, toggle Broadcast, campo IP, estado. Config persistida en el estado. Validado: compila Release (VST3 + Standalone).
- **Salida DMX por red — sACN E1.31 (UDP 5568):** `SacnSender` (paquete E1.31 completo: root/framing/DMP, CID por instancia, prioridad, secuencia por universo). Multicast a 239.255.x.x (auto por universo) o unicast. Universo interno `u` → sACN `u+1`. Integrado en `sendDmxToNetwork()` (emite Art-Net y/o sACN). UI en `OutputPanel`: toggle sACN, toggle Multicast, IP, prioridad, estado. Persistido. Compila Release OK.
- **Salida DMX por hardware — Enttec USB Pro:** `SerialPort` (puerto serie multiplataforma: Win32 `CreateFile`/DCB 8N2 + POSIX termios) + `EnttecSender` (mensaje USB Pro label 6: `0x7E 06 lenLo lenHi 00 <512ch> 0xE7`). Selección de puerto, universo emitido y reapertura automática si falla. UI en `OutputPanel`: toggle Enttec, combo de puertos (con botón Refrescar), universo, estado de conexión en vivo. Persistido. Compila Release OK.
- **Build:** validado en Release (MSVC), corre en Standalone.

### 🚧 Pendiente (proyecto actual)
1. (Salida DMX COMPLETA: Art-Net + sACN E1.31 + Enttec USB Pro, todas HECHAS y emitiendo desde el timer del processor.)
2. (Pulido scrub/regla: cursor "mano" + badge compás:beat al arrastrar — HECHO.)
3. (Waveform: cursor de reproducción + click-to-seek — HECHO.)
4. (Auto-color por tipo de canal al crear fixtures — HECHO.)
5. (Timeline: clips de efecto encima de keyframes — HECHO, modelo mixto.)
6. Probar salida real: Art-Net/sACN con un visualizador; Enttec con hardware FTDI.

### 🤖 Target: Standalone "AI Automator" Edition (EN CURSO — Fase 6 hecha)
App para DJs/eventos: playlist + coreografía **AI offline**. Target `LuxAutomator` (juce_add_gui_app) dentro de `dmx-vst`, reutilizando `source/` (de momento `LuxLookAndFeel.h`). Código en `apps/automator/`.

**Roadmap por fases:**
- ✅ **Fase 1 — Decodificación + Playlist:** `Track` (estado + duración), `PlaylistManager` (añadir/quitar/reordenar + lee metadata), `AudioPlayer` (AudioDeviceManager + transporte), `AutomatorComponent` (UI: lista, botones, transporte, encadena temas). `Main.cpp` (app JUCE). Compila Release OK.
- ✅ **Fase 2 — Worker pool + JobQueue + Sidecar:** `PreprocessWorker` (juce::ThreadPool, ids estables, dispatch seguro al hilo de mensajes vía WeakReference), `OfflineAnalyzer` *stub* (envolvente de energía RMS real), `SidecarStore` (cache `.lux` validada por firma del archivo). Estados visibles Pendiente→Analizando→Listo. Compila Release OK.
- ✅ **Fase 3 — Análisis DSP offline:** `OfflineAnalyzer` reutiliza `source/AudioAnalyzer` (FFT por bandas + onsets) → envolvente de energía, transientes y **BPM** estimado (histograma de intervalos entre onsets, doblado a 70–170). Schema sidecar v2 (transientes serializados). UI muestra el BPM junto al estado. Compila Release OK.
- 🚧 **Fase 4 — Separación de stems (HTDemucs):** ✅ HECHA (camino A, sidecar Python). `StemSeparator` (interfaz) + `DemucsProcessProvider` (invoca `demucs`, decodifica a WAV con JUCE, cachea en `%APPDATA%/LuxSync/stems`). `TrackAnalysis` schema v3 con análisis por stem. `ChoreographyEngine` mapea por instrumento (batería→PAR/strobe, bajo→barra, voces→acentos cabezas, melodía→movimiento). Toggle "Stems IA" en UI. Degrada al camino mono si demucs no está. Requiere Python 3.11 + `demucs torch==2.2.2 torchaudio==2.2.2 soundfile "numpy<2"` (per-user, sin admin). Compila Release OK.
- ✅ **Fase 5 — ChoreographyEngine → DmxShow:** `DefaultRig` (4 PAR RGBW + 2 Wash RGBW + barra RGB + 2 cabezas + 2 spiders + 2 strobes), `ChoreographyEngine` (energía→dimmer con contraste + **destellos nítidos** snap+caída en transientes, **color por beat** distinto por equipo, strobes reales, Pan/Tilt con LFO), `DmxShow` (rig + automatización, `renderFrame(seg)` muestrea a buffer DMX). Se genera por tema al terminar el análisis; UI marca "show". Compila Release OK.
- 🚧 **Fase 5 — ChoreographyEngine:** mapeo auto → `DmxShow` (Drums→PAR/strobe, Bass→LED bars, Vocals→efecto central, Other→moving heads).
- ✅ **Fase 6 — PlaybackEngine lock-free:** Art-Net sincronizado frame-a-frame + preview en UI.
- 🚧 **Fase 7 — Pulido UI + presets de routing.** Parcial HECHO: persistencia de playlist (auto guardar/cargar sesión) + **visualizador de escenario** (luces simuladas: PAR/wash/barra/cabeza/spider/strobe con haces) + toggle Escenario/Niveles + **barra de progreso por fila** + coreografía mucho más potente + Stems IA serializado/re-analizable. Falta: presets de routing (elegir rig/mapeo), opc. shuffle/orden + editor de rig.
- **Protocolo:** fases modulares, NO volcar boilerplate de golpe, preguntar antes de C++ pesado. ONNX pospuesto a Fase 4.

---

## Estrategia de suite + modelo de negocio (decidido 2026-06)

- El proyecto actual (LuxSync DMX) queda **aparte**; la **suite (~16 plugins)** será un **repo separado a futuro**.
- **Suite (plan):** monorepo con `core/` static lib compartida (dsp/model/dmx/ipc/ui) + 1 target fino por plugin vía helper `lux_add_plugin()`, `plugins/_template` para clonar, `apps/` para standalones.
- **Comercial:** vender plugins **sueltos, en bundles y suite completa**.
  - Cada target produce su `.vst3` **independiente** (compartir `core` no une binarios).
  - **Licencia por plugin** (cada `.vst3` valida su clave; uno no desbloquea a otros).
  - Manufacturer code común + plugin code único por VST3. Marca **sin definir** aún.
- **GitHub:** 1 repo privado monorepo (sin submódulos), **GitHub Actions CI** compilando Release MSVC → artifacts (.vst3/.exe), porque se compila en otra PC. Releases + tags por hito.
- Pendiente suite: sistema de **licencias por plugin** + scripts de **empaquetado de instaladores** (suelto / bundle / suite).

---

## Bitácora de sesiones

### 2026-06-11 (cont.) — AI Automator: coreografía potente + Stems IA usables + barra de progreso
- **Queja de Aragon:** el modo sin IA daba un lightshow "flojo y parejo" — todas las luces igual, sin parpadeo (solo subía/bajaba un poco la intensidad), color cambiando muy lento.
- **`ChoreographyEngine::paintFixture` reescrito (mucho más punchy):**
  - Suelo de intensidad bajado `0.12 → 0.04` + curva de contraste `pow(e, 1.8)` y paso fino `dimStep 0.08`. Las partes flojas quedan oscuras y los golpes destacan.
  - **Destellos nítidos** (`addFlash`): keyframe `stepped` que mantiene la base hasta el golpe → **SNAP** a tope (rampa) → caída rápida a base en `0.14 s`. Antes era una rampa suave (de ahí "no parpadea").
  - **Color por BEAT** (no por compás) y **offset por equipo** (`colorOffset = fixtureIndex`): cada PAR/wash arranca en un color distinto y cambia al ritmo → escenario multicolor. Cabezas cambian cada 2 beats.
  - Strobe: umbral de disparo bajado `0.55 → 0.40`. Pan/Tilt más vivos (Tilt periodo `5 → 4`).
  - `PaintContext` ganó `fixtureIndex` + `bpm`; ambos generadores los pasan.
- **Stems IA ahora usables (antes congelaban el PC):**
  - `DemucsProcessProvider`: `std::mutex separateMutex` serializa la separación → **un solo demucs a la vez** (antes el `ThreadPool` lanzaba varios en paralelo y, como torch ya abre ~8 procesos Python, saturaba todos los núcleos).
  - `PreprocessWorker`: `cacheUsable = valid && (!needStems || !stems.empty())` → si pides stems y el sidecar es mono, **fuerza re-análisis** (antes cargaba el mono de caché y nunca separaba).
  - `PlaylistManager::reanalyzeAll()` re-encola todos los temas con el modo actual; el toggle **Stems IA** lo llama → activar el toggle re-genera los shows con stems (reusa la caché de stems `.wav` si existe).
  - `stemsEnabled` por defecto **FALSE** (no descarga el modelo al abrir; análisis mono rápido). Stems IA es lento en CPU (~minutos/tema la 1ª vez + descarga del modelo una sola vez), se cachea.
- **Barra de progreso por fila** (`paintListBoxItem`): **Analizando** = barra azul indeterminada (segmento que recorre, `animPhase += 1/30` en el timer si hay algún tema analizándose); **Listo + show** = barra ámbar llena; **Pendiente** = fondo tenue.
- **Bug de la sesión anterior resuelto:** la sesión guardada quedó con `stems="1"` (default viejo) → al reabrir lanzaba 8 Python y dejaba el tema "Pendiente" eterno; además el `.exe` con la barra no se actualizaba por `LNK1104` (app abierta). Solución: matar Python (`taskkill /F /T` por PID), cerrar la app antes de compilar, sesión a `stems="0"`.
- **Build:** todo headers salvo el edit de `AutomatorComponent.cpp` (ya en CMake) → Release OK. `LuxSync AI Automator.exe` regenerado y abierto.
- **Siguiente paso:** probar el lightshow nuevo (sin IA debería verse mucho más vivo; luego con Stems IA para show por instrumento). Fase 7 restante: presets de routing (elegir rig/mapeo desde UI), opc. shuffle/orden de reproducción + editor de rig.

### 2026-06-11 (cont.) — AI Automator: Fase 7 (persistencia de playlist + visualizador de escenario)
- **Persistencia de playlist (`PlaylistManager`):** `saveSession()`/`loadSession()` + `defaultSessionFile()` = `%APPDATA%/LuxSync/automator_session.xml`. Guarda las rutas de los temas + el flag de stems. Auto-guarda tras añadir/quitar/reordenar/limpiar (flag `restoring` suprime el guardado mientras se carga). `loadSession()` re-encola el análisis de cada tema (reusa la cache sidecar `.lux`, así que es barato). `AutomatorComponent` llama `loadSession()` en el ctor (tras `addChangeListener`), re-sincroniza el toggle de stems y `list.updateContent()`.
- **Visualizador de escenario (`StageVisualizer.h`, header-only):** misma API que `DmxPreview` (`setShow`/`refreshFrom(engine)`), pero dibuja las luces **simuladas** en vez de medidores. Infiere el tipo de equipo (`Kind` = Par/Wash/Bar/MovingHead/Spider/Strobe/Generic) por nombre+modelo+canales. Lee el frame DMX y por cada equipo extrae color (R/G/B + White/Amber/UV), intensidad (Dimmer×color), Pan/Tilt y Strobe. Escena en bandas: truss arriba (cabezas+spiders con haz orientado por Pan/Tilt / abanico de haces), wash debajo (cono ancho), barra LED media (segmentos + halo), suelo (PAR con halo hacia arriba + strobe con fogonazo blanco). 
- **Rig ampliado (`DefaultRig`):** añadidos 2× Wash RGBW, 2× Spider (Pan/Tilt/Dim/RGB) y 2× Strobe (Dim/Strobe) además de los 4 PAR, barra y 2 cabezas, para que el escenario muestre variedad. El `ChoreographyEngine` los mapea genéricamente por tipo de canal (spiders→como cabezas, washes/strobes→como PAR/drums).
- **UI (`AutomatorComponent`):** toggle **Escenario / Niveles** (por defecto Escenario) que alterna `StageVisualizer`↔`DmxPreview` en el área de preview. El `timerCallback` refresca solo la vista activa.
- **Build:** todo headers salvo el edit de `AutomatorComponent.cpp` (ya en CMake) → **sin reconfigurar**. Release OK, sin warnings, `LuxSync AI Automator.exe` generado.
- **Siguiente paso:** Fase 7 — presets de routing (elegir rig/mapeo desde la UI) y probar el flujo completo con un MP3 real; opcional: orden de reproducción/shuffle y editar el rig.

### 2026-06-11 (cont.) — AI Automator: Fase 4 (separación de stems con HTDemucs)
- **Decisión (Aragon):** el mejor modelo libre sin importar peso/CPU, solo Standalone (DJs/eventos). Elegido **HTDemucs** (Demucs v4, Meta, MIT) → 4 stems Drums/Bass/Vocals/Other.
- **Vía elegida = camino A (sidecar Python):** se invoca `demucs` como proceso externo, NO ONNX en C++ (HTDemucs es híbrido waveform+espectro+transformer; no hay `.onnx` oficial y reimplementarlo sería enorme y frágil). Da el modelo REAL, gratis, offline y cacheado.
- **Entorno instalado y validado (todo per-user, SIN admin):** Python 3.11.4 + `pip install demucs torch==2.2.2 torchaudio==2.2.2 soundfile "numpy<2"` + **ffmpeg portable** en `~/tools/ffmpeg` (en PATH). OJO con versiones: torch 2.12/torchaudio 2.11 exigen `torchcodec` (sus DLLs no cargan en Win); torch 2.2.2 exige `numpy<2`. `soundfile` = backend WAV nativo sin DLLs externas. Comando validado: `python -m demucs -n htdemucs -o <out> <wav>` → `<out>/htdemucs/{drums,bass,vocals,other}.wav` (~22 s para 6 s de audio en CPU; los pesos se descargan la 1ª vez).
- **`StemSeparator.h`:** interfaz desacoplada + `StemSet` (4 archivos). Permite cambiar a un backend ONNX en el futuro sin tocar el resto.
- **`DemucsProcessProvider.h`:** implementación con `juce::ChildProcess`. Detecta Python (`LUX_PYTHON`/`python`/`py -3`/`python3`), **decodifica la entrada a WAV temporal con JUCE** (no depende de ffmpeg en runtime), invoca demucs con `--filename "{stem}.{ext}"` y cachea en `%APPDATA%/LuxSync/stems/<firma>/htdemucs/`. Cancelable; reutiliza la cache si ya existe.
- **`TrackAnalysis.h` schema v3:** añadido `std::vector<StemAnalysis> stems` (nombre + energía + transientes por instrumento), serializado en nodos hijos `Stem`. Helpers `packEnergy/unpackEnergy/packTransients/unpackTransients`. v3 invalida caches v2 automáticamente.
- **`OfflineAnalyzer.h`:** tras el análisis de la mezcla, si hay separador disponible separa stems y analiza cada uno (energía+transientes). Es **opcional**: si falla o no está, conserva el camino mono.
- **`ChoreographyEngine.h` refactor:** `paintFixture(PaintContext)` reutilizable. `generateMono` (Fase 5, sin stems) y `generateFromStems` (mapeo por instrumento): **batería→PAR/strobe**, **bajo→barra LED**, **voces→acentos de intensidad en cabezas**, **melodía/otros→movimiento + color de cabezas**. `generate()` elige automáticamente según haya stems.
- **`PreprocessWorker.h`:** posee un `DemucsProcessProvider` + flag `stemsEnabled` + carpeta de cache; pasa el separador al `OfflineAnalyzer`. `PlaylistManager` reenvía `setStemsEnabled`/`isStemSeparationAvailable`/`getStemBackendName`.
- **UI (`AutomatorComponent`):** toggle **"Stems IA"** + etiqueta de estado del backend (se deshabilita si no hay Python/demucs); indicador **"stems"** (cian) vs **"show"** (ámbar) en la lista.
- **Build:** todo headers (sin reconfigurar CMake por archivos nuevos) → **Release OK**, `LuxSync AI Automator.exe` generado.
- **Siguiente paso:** Fase 7 — pulido UI + presets de routing (elegir rig/mapeo) + persistir playlist; y probar el flujo completo añadiendo un MP3 real (la 1ª separación descarga los pesos del modelo).

### 2026-06-11 (cont.) — AI Automator: Fase 6 (PlaybackEngine + Art-Net + preview)
- **`PlaybackEngine.h`:** mantiene el show activo (puntero al `DmxShow` del Track) bajo `std::mutex` breve. `tick(seg)` (desde el timer de UI, 30 Hz) llama `renderFrame` y emite cada universo por Art-Net si está activo. `blackout()` apaga todo. `copyLatestUniverse()` deja a la UI leer el último frame. Reusa `source/ArtNetSender` (`artNet()` para configurar enabled/IP/broadcast).
- **`DmxPreview.h`:** componente que, por cada equipo del show, dibuja un "punto de color" (R/G/B escalado por Dimmer) + medidores verticales por canal coloreados por tipo. `refreshFrom(engine)` copia el frame y repinta; sin timer propio.
- **`AutomatorComponent`:** añadidos `PlaybackEngine engine`, `DmxPreview preview`, toggle **Art-Net** + editor de **IP** (vacío=broadcast, por defecto 127.0.0.1 unicast). `timerCallback` (subido a 30 Hz) hace `engine.tick(pos)` + `preview.refreshFrom`. `updateActiveShow()` re-resuelve el puntero del show en play/stop/next y en cada `changeListenerCallback` (el vector de tracks puede reubicarse al añadir/quitar). Ventana 900×640. Dtor desactiva Art-Net.
- **CMake:** `LuxAutomator` ahora compila `source/ArtNetSender.cpp` (reconfigurado). Build Release OK.
- **Cómo verlo:** abrir un visualizador Art-Net en localhost (o un nodo en la LAN), activar el toggle, poner su IP, darle a Play → las luces se mueven con la música. El preview interno funciona siempre, sin red.
- **Siguiente paso:** Fase 7 — pulido de UI + presets de routing (elegir rig/mapeo), y opcionalmente persistir la playlist. La Fase 4 (ONNX/stems) sigue pendiente de decisión de Aragon (descarga de runtime+modelo).

### 2026-06-11 (cont.) — AI Automator: Fase 5 (ChoreographyEngine → DmxShow)
- **Decisión:** saltar la Fase 4 (ONNX/stems, requiere descargas pesadas) y hacer la Fase 5, que ya da show visible con onsets+BPM+energía de la mezcla. ONNX se enchufa después sin tirar nada.
- **`DmxShow.h`:** modelo del show = `std::vector<Fixture>` (con keyframes/clips rellenos) + bpm + lengthSeconds + numUniverses. `Universe=std::array<uint8,512>`; `renderFrame(seg, out)` convierte seg→beats y muestrea cada canal con `evaluateChannel` (reusa `source/Sequence.h`), escribe DMX. Solo lectura en playback (lock-free).
- **`DefaultRig.h`:** rig por defecto (universo 0, direcciones consecutivas): 4× PAR RGBW+Dim, 1× barra LED RGB, 2× cabeza móvil (Pan/Tilt/Dim/RGB). Para generar/previsualizar sin editor de equipos.
- **`ChoreographyEngine.h`:** `generate(TrackAnalysis, rig)→DmxShow`. Mapeo mono-mix: Dimmer/White siguen la envolvente de energía (cada 0.12s, con suelo 0.12) + destellos en transientes (255→energia); R/G/B ciclo de color por compás (paleta de 8, keyframes en salto, offset por equipo); Strobe/Shutter pulso en transientes fuertes (energía>0.55); Pan/Tilt con `EffectClip` LFO (seno 8 beats / triangular 5 beats, desfase por cabeza). Todo en beats al BPM estimado (fallback 120).
- **Wiring:** `Track` lleva `DmxShow show`; `PlaylistManager` posee el `rig` (DefaultRig) y genera el show en `onAnalysisComplete`. UI: indicador "show" ámbar en la lista cuando está generado. Todo headers (salvo el edit del .cpp de UI) → sin reconfigurar CMake. Compila Release OK.
- **Siguiente paso:** Fase 6 — `PlaybackEngine` lock-free que muestrea el `DmxShow` del tema en reproducción y lo emite por **Art-Net** (UDP 6454), más un **preview** de niveles DMX en la UI para ver el show en vivo.

- **`OfflineAnalyzer.h` reescrito:** ahora reutiliza `source/AudioAnalyzer` (FFT por bandas + deteccion de onsets) en vez del stub RMS. Saca: envolvente de energia gruesa (de los peaks de la waveform, 512 puntos), lista de transientes (onsets) y **BPM** estimado. `estimateBpm()` = histograma de intervalos entre onsets (hasta 4 de separación), cada candidato doblado/dividido al rango [70,170), suavizado ±1 bin, pico = BPM. `energyFromPeaks()` reduce y normaliza. Cancelable.
- **`TrackAnalysis.h` schema v2:** añadido `std::vector<double> transients` serializado en base64 (floats). `kSchemaVersion`=2 invalida caches v1 automáticamente.
- **CMake:** `LuxAutomator` ahora compila `source/AudioAnalyzer.cpp` y enlaza `juce::juce_dsp`. Reconfigurado + build Release OK.
- **UI:** `AutomatorComponent::paintListBoxItem` muestra `"<estado>  <BPM> BPM"` cuando el análisis es válido (columna de estado ensanchada a 130 px).
- **Siguiente paso:** Fase 4 — separación de stems con ONNX Runtime (C++ pesado; **preguntar a Aragon antes**: descarga de runtime/modelo, tamaño, licencias).

- **`TrackAnalysis.h`:** struct cacheable (lengthSeconds, estimatedBpm, energy[] normalizada) + serialización ValueTree→XML; `kSchemaVersion`=1 y firma del origen (tamaño+fecha) para invalidar cache. Energía empaquetada base64.
- **`SidecarStore.h`:** load/save del `.lux` junto al audio; `load` valida firma y devuelve inválido si la cache está obsoleta.
- **`OfflineAnalyzer.h`:** analizador *stub* funcional — decodifica el archivo por bloques y calcula envolvente RMS gruesa (512 puntos) normalizada; cancelable vía `std::function<bool()>`. BPM pospuesto a Fase 3.
- **`PreprocessWorker.h`:** `juce::ThreadPool` (nCpus-1). Cada `Job` comprueba cache → si no, analiza + guarda sidecar. Resultado devuelto en el **hilo de mensajes** vía `MessageManager::callAsync` + `JUCE_DECLARE_WEAK_REFERENCEABLE` (seguro ante destrucción; el dtor hace `removeAllJobs(true)`). Callbacks `onStarted`/`onComplete`.
- **`Track.h`:** añadido `int id` estable + `TrackAnalysis analysis` + `errorMessage`. `Pending` ahora = "añadido, sin procesar".
- **`PlaylistManager.h`:** asigna ids (`nextId++`), encola pre-proceso al añadir; `indexForId` + `onAnalysisStarted`/`onAnalysisComplete` actualizan estado (Pendiente→Analizando→Listo) y `sendChangeMessage`. Todo headers → sin reconfigurar CMake. Compila Release OK.
- **Siguiente paso:** Fase 3 — `OfflineAnalyzer` real (reusar `source/AudioAnalyzer`): transientes/estructura/BPM, ampliando el schema del sidecar.

### 2026-06-11 (cont.) — AI Automator: diseño + Fase 1 (playlist + reproductor)
- **Milestone 1 (diseño):** arquitectura de módulos (App Shell → PlaylistManager/SidecarStore + WorkerPool[Decode→Stem→Analyze→Choreography] → PlaybackEngine/DmxShowReader/DmxOutputHub) + roadmap de 7 fases + matriz de mapeo auto. Decisiones: target dentro de `dmx-vst` reutilizando `source/`; ONNX pospuesto a Fase 4.
- **Fase 1 implementada** (`apps/automator/`): `Track.h` (estado Pending/Ready/Decoding/Analyzing/Choreographed/Error + duración), `PlaylistManager.h` (ChangeBroadcaster; añadir/quitar/reordenar + lee duración con AudioFormatManager), `AudioPlayer.h` (AudioDeviceManager + AudioSourcePlayer + AudioTransportSource; load/play/pause/stop/seek + hasReachedEnd), `AutomatorComponent.{h,cpp}` (UI tema oscuro LuxLookAndFeel: ListBox de temas con nº/estado/duración + indicador ▶, botones Añadir/Quitar/Subir/Bajar, transporte Play-Pause/Stop + slider de posición + tiempo + "now playing"; doble clic reproduce; encadena al siguiente al terminar), `Main.cpp` (juce app + DocumentWindow).
- **CMake:** nuevo target `juce_add_gui_app(LuxAutomator ...)` (PRODUCT_NAME "LuxSync AI Automator") con juce_audio_utils/audio_formats/gui_extra. Reconfigurar requerido (target nuevo). Compila Release OK → `build/LuxAutomator_artefacts/Release/LuxSync AI Automator.exe`.
- **Siguiente paso:** Fase 2 — WorkerPool + JobQueue + SidecarStore (cola de pre-proceso en background con estados visibles; analizador stub + cache `.lux`).

### 2026-06-11 (cont.) — Clips de efecto (LFO) en el timeline (modelo mixto)
- **Backend:** `Sequence.h` ahora define `EffectType` (Sine/Triangle/SawUp/SawDown/Square/Random), `EffectClip` (start/length/period/low/high/phase en beats), `effectWaveform()`, `evaluateEffectClip()` y `evaluateChannel()` (clip activo sobrescribe keyframes; si no hay clip, interpola keyframes). `ChannelDef` gana `std::vector<EffectClip> clips`. Serialización de clips en `FixtureModel.cpp` (nodos `Clip`) + conversores `effectTypeToString/FromString`/`allEffectTypeNames`. `renderDmxFrame` usa `evaluateChannel`.
- **UI** (`TimelinePanel`): toggle **Efectos** en la barra de controles. Con el modo activo: arrastrar en una pista crea un clip y redimensiona su borde derecho; arrastrar el cuerpo lo mueve; arrastrar los bordes lo redimensiona; doble clic abre un `AlertWindow` (forma de onda + periodo + min/max + fase); clic derecho lo borra. Cursores contextuales (mano / redimensión). Dibujo: bandas translúcidas con el color del canal, asas en los bordes, la forma de onda muestreada dentro y etiqueta del tipo.
- Compila Release OK (VST3 + Standalone), sin warnings.
- **Siguiente paso:** probar salida real (visualizador Art-Net/sACN o hardware Enttec) o empezar el diseño del **AI Automator** (solo arquitectura + roadmap, sin C++ pesado).

### 2026-06-11 (cont.) — Pulidos de UI hacia versión liberable
- **Auto-color por tipo de canal:** `defaultColourForChannelType()` en `FixtureModel` (Rojo→rojo, Verde→verde, Azul→azul, Blanco, Ámbar, UV→violeta, Dimmer→ámbar cálido, Strobe→cian, Color→rosa, Gobo, Pan/Tilt→azul gris, Generic→cian). Se aplica al crear canales en `FixtureEditor` (plantillas y botón "+ Canal") y al cambiar el tipo en `ChannelRow`. Corregido bug previo: `ChannelRow::getChannelDef()` perdía `colour`+`keyframes` al editar — ahora guarda `source` y solo recolorea si el tipo cambió.
- **Cursor de reproducción en la waveform** (`WaveformView`): línea verde + glow en la posición actual, `setPlayheadSeconds()` + `onSeek` callback; click/arrastre sobre la onda hace seek (`seekTimeForX`). `AudioPanel` ahora es `juce::Timer` (30 Hz) que actualiza el cursor con `getPlaybackSeconds()` y conecta `onSeek → seekToSeconds`. (Processor ya exponía `getPlaybackSeconds()`/`seekToSeconds()`.)
- **Pulido scrub/regla** (`TimelinePanel`): cursor "mano" (`PointingHandCursor`) al pasar por la regla de compases; badge flotante compás.beat junto al playhead mientras se arrastra (`scrubbing`).
- Compila Release OK (VST3 + Standalone) en cada paso.
- **Siguiente paso:** timeline (clips de efecto, modelo mixto) o empezar el diseño del **AI Automator** (solo arquitectura + roadmap, sin C++ pesado).

### 2026-06-11 (cont.) — Salida DMX Enttec USB Pro (hardware serie)
- **`source/SerialPort.{h,cpp}`** (nuevo): puerto serie multiplataforma. Windows: `CreateFileA` con prefijo `\\.\` (para COM10+), DCB 8N2, `getAvailablePorts()` sondeando COM1..256. POSIX: termios `cfmakeraw` + CS8/CSTOPB, lista `/dev/tty.usb*`,`cu.usb*`,`ttyUSB*`,`ttyACM*`.
- **`source/EnttecSender.{h,cpp}`** (nuevo): protocolo Enttec USB Pro — mensaje label 6 `0x7E 06 lenLo lenHi <startcode 0x00> <hasta 512 ch> 0xE7` a 250000 baud (el device marca el timing DMX real). Selección de puerto + universo emitido; si falla `write`, cierra y reintenta abrir en el siguiente envío.
- Integrado en el processor (miembro `enttec`, API set/get, persistencia, envío en `sendDmxToNetwork()` del universo elegido). UI en `OutputPanel` (4ª fila: toggle + combo de puertos + Refrescar + universo + estado de conexión, refrescado en vivo por el timer). Añadido a CMake. Compila Release OK (VST3 + Standalone).
- **Salida DMX COMPLETA:** Art-Net + sACN E1.31 + Enttec USB Pro.
- **Siguiente paso:** pulidos de UI (scrub/regla, cursor en waveform, auto-color por tipo) o empezar diseño del AI Automator.

### 2026-06-11 (cont.) — Render/envío DMX movido al processor
- El processor ahora hereda también `juce::Timer` (hilo de mensajes) ademas del `HighResolutionTimer` (transporte). `timerCallback()` hace `renderDmxFrame(getPlayheadBeats()) + sendDmxToNetwork()` a ~44 Hz. Así el DMX se emite aunque la ventana del editor esté cerrada.
- Calificadas las llamadas a `startTimer`/`stopTimer` por clase base (ambas bases las definen): `juce::HighResolutionTimer::` y `juce::Timer::`.
- `OutputPanel::timerCallback` ya NO renderiza ni envía; solo lee el buffer para mostrarlo (mismo hilo, sin doble render ni race).
- Compila Release OK (VST3 + Standalone).
- **Siguiente paso:** Enttec USB Pro (salida serie FTDI).

### 2026-06-11 (cont.) — Salida DMX sACN E1.31
- **Implementado sACN E1.31:** nuevo `source/SacnSender.{h,cpp}` (paquete E1.31 root+framing+DMP, CID por instancia vía `juce::Uuid`, prioridad, secuencia por universo, multicast 239.255.x.x auto o unicast, puerto 5568). Integrado en `sendDmxToNetwork()` junto a Art-Net (universo interno u → sACN u+1). UI en `OutputPanel` (3ª fila: toggle sACN + Multicast + IP + prioridad + estado). Persistencia en el estado. Añadido a CMake. Compila Release OK (VST3 + Standalone).
- **Siguiente paso:** Enttec USB Pro (salida serie FTDI), y mover el tick de render/envío a un `HighResolutionTimer` del processor (emitir con la ventana Main cerrada).

### 2026-06-11 — Salida DMX Art-Net + registro de contexto
- Reencontrado el proyecto en `C:\Users\Aragonlu\dmx-vst` y abierto en VS Code.
- Verificado: los debug logs de sesiones previas NO guardan transcript recuperable (solo metadata). El contexto se conserva en memoria persistente de la IA + este DEVLOG.
- Creado este **DEVLOG.md** + nota de memoria de repo para recuperación rápida.
- **Implementado Art-Net (salida DMX por red):** nuevo `source/ArtNetSender.{h,cpp}` (paquete ArtDmx, broadcast/unicast, secuencia por universo, `juce::DatagramSocket`). Wiring en processor (`sendDmxToNetwork`, emite universos usados, persistencia). UI en `OutputPanel` (toggle Art-Net + Broadcast + IP + estado). Añadido a CMake. Compila Release OK (VST3 + Standalone).
- **Siguiente paso:** probar con un visualizador Art-Net (p.ej. en localhost) y luego implementar sACN E1.31; después mover el tick de render/envío a un `HighResolutionTimer` del processor.

<!--
Plantilla para nuevas entradas:
### YYYY-MM-DD — Título
- Qué se hizo:
- Decisiones:
- Siguiente paso:
-->
