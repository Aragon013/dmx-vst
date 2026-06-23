#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../apps/automator/ChoreographyEngine.h"
#include "../apps/automator/OfflineAnalyzer.h"

DmxVstAudioProcessor::DmxVstAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Reloj propio (independiente del audio) para el transporte interno en Standalone.
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    juce::HighResolutionTimer::startTimer (16);   // ~60 Hz: avanza el transporte interno

    // Timer del hilo de mensajes: renderiza y emite DMX siempre (aunque no haya
    // ventana de editor abierta). ~44 Hz, refresco tipico DMX.
    juce::Timer::startTimerHz (44);
}

DmxVstAudioProcessor::~DmxVstAudioProcessor()
{
    juce::HighResolutionTimer::stopTimer();
    juce::Timer::stopTimer();
}

void DmxVstAudioProcessor::hiResTimerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    double dt = (now - lastTickMs) / 1000.0;
    lastTickMs = now;

    // Proteccion ante saltos grandes (p.ej. al despertar de suspension).
    if (dt < 0.0 || dt > 0.5)
        dt = 0.0;

    if (transport.hasPlayHead.load())
    {
        // Manda el host (DAW): no avanzamos el transporte interno.
    }
    else if (hasAudioFile.load())
    {
        // La posicion la marca el propio audio que esta sonando.
        const double secs = transportSource.getCurrentPosition();
        internalBeats.store (secs * internalBpm.load() / 60.0);
        internalPlaying.store (transportSource.isPlaying());
    }
    else if (internalPlaying.load())
    {
        // Sin audio: avanzamos por reloj de pared al BPM indicado.
        const double bpm = internalBpm.load();
        double b = internalBeats.load() + dt * bpm / 60.0;

        const double len = internalLength.load();
        if (len > 0.0)
        {
            if (internalLoop.load())
            {
                while (b >= len)
                    b -= len;
            }
            else if (b >= len)
            {
                b = len;
                internalPlaying.store (false);
            }
        }

        internalBeats.store (b);
    }
}

void DmxVstAudioProcessor::timerCallback()
{
    // Render + salida DMX en el hilo de MENSAJES (igual que las ediciones de
    // fixtures/keyframes desde la UI, por eso es seguro sin locks). Este timer
    // lo posee el processor, asi que sigue emitiendo aunque la ventana del
    // editor este cerrada (necesario en produccion).
    renderDmxFrame (getPlayheadBeats());
    sendDmxToNetwork();
}

//==============================================================================
void DmxVstAudioProcessor::transportPlay() noexcept
{
    internalPlaying.store (true);
    if (hasAudioFile.load())
        transportSource.start();
}

void DmxVstAudioProcessor::transportStop() noexcept
{
    internalPlaying.store (false);
    if (hasAudioFile.load())
        transportSource.stop();
}

void DmxVstAudioProcessor::transportToggle() noexcept
{
    if (internalPlaying.load())
        transportStop();
    else
        transportPlay();
}

void DmxVstAudioProcessor::transportRewind() noexcept
{
    internalBeats.store (0.0);
    if (hasAudioFile.load())
    {
        const juce::ScopedLock sl (audioLock);
        transportSource.setPosition (0.0);
    }
}

void DmxVstAudioProcessor::seekToBeats (double beats) noexcept
{
    const double b = juce::jmax (0.0, beats);
    internalBeats.store (b);

    if (hasAudioFile.load())
    {
        const double bpm = internalBpm.load();
        const double secs = (bpm > 0.0) ? b * 60.0 / bpm : 0.0;
        const juce::ScopedLock sl (audioLock);
        transportSource.setPosition (secs);
    }
}

void DmxVstAudioProcessor::setInternalLoop (bool shouldLoop) noexcept
{
    internalLoop.store (shouldLoop);
    const juce::ScopedLock sl (audioLock);
    if (readerSource != nullptr)
        readerSource->setLooping (shouldLoop);
}

double DmxVstAudioProcessor::getPlaybackSeconds() noexcept
{
    if (! hasAudioFile.load())
    {
        const double bpm = internalBpm.load();
        return (bpm > 0.0) ? internalBeats.load() * 60.0 / bpm : 0.0;
    }

    const juce::ScopedLock sl (audioLock);
    return transportSource.getCurrentPosition();
}

void DmxVstAudioProcessor::seekToSeconds (double seconds) noexcept
{
    const double bpm = internalBpm.load();
    seekToBeats ((bpm > 0.0) ? seconds * bpm / 60.0 : 0.0);
}
bool DmxVstAudioProcessor::loadAudioForPlayback (const juce::File& file)
{
    playbackFormatManager.registerBasicFormats();

    auto* reader = playbackFormatManager.createReaderFor (file);
    if (reader == nullptr)
        return false;

    const double lenSec = (reader->sampleRate > 0.0)
                        ? (double) reader->lengthInSamples / reader->sampleRate
                        : 0.0;

    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    newSource->setLooping (internalLoop.load());

    {
        const juce::ScopedLock sl (audioLock);
        transportSource.stop();
        transportSource.setSource (nullptr);
        readerSource = std::move (newSource);
        transportSource.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
        transportSource.prepareToPlay (preparedBlockSize, preparedSampleRate);
        transportSource.setPosition (0.0);
    }

    audioLengthSec.store (lenSec);
    audioFileName = file.getFileName();
    hasAudioFile.store (true);
    internalPlaying.store (false);
    internalBeats.store (0.0);
    return true;
}

void DmxVstAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    live.prepare (sampleRate);

    preparedSampleRate = sampleRate;
    preparedBlockSize  = samplesPerBlock;
    transportSource.prepareToPlay (samplesPerBlock, sampleRate);
}

void DmxVstAudioProcessor::releaseResources()
{
    transportSource.releaseResources();
}

bool DmxVstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

void DmxVstAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Leer el transporte del DAW (BPM, posicion, play/stop, compas).
    // En Standalone NO usamos el playhead (el wrapper puede ofrecer uno "muerto"
    // que reporta parado en 0); ahi manda nuestro transporte interno.
    const bool standalone = (wrapperType == wrapperType_Standalone);

    if (! standalone)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                transport.hasPlayHead.store (true);
                transport.isPlaying.store (pos->getIsPlaying());

                if (auto bpm = pos->getBpm())
                    transport.bpm.store (*bpm);

                if (auto secs = pos->getTimeInSeconds())
                    transport.timeSeconds.store (*secs);

                if (auto ppq = pos->getPpqPosition())
                    transport.ppqPosition.store (*ppq);

                if (auto sig = pos->getTimeSignature())
                {
                    transport.timeSigNum.store (sig->numerator);
                    transport.timeSigDen.store (sig->denominator);
                }
            }
            else
            {
                transport.hasPlayHead.store (false);
            }
        }
        else
        {
            transport.hasPlayHead.store (false);
        }
    }
    else
    {
        transport.hasPlayHead.store (false);
    }

    // Reproduccion del archivo de audio cargado (suena por la salida y, al
    // avanzar su posicion, mueve el transporte/playhead en Standalone).
    if (hasAudioFile.load())
    {
        const juce::ScopedTryLock stl (audioLock);
        if (stl.isLocked() && readerSource != nullptr)
        {
            juce::AudioSourceChannelInfo info (&buffer, 0, buffer.getNumSamples());
            transportSource.getNextAudioBlock (info);   // rellena el buffer con el audio
        }
        else
        {
            buffer.clear();
        }
    }

    // Analisis en vivo: si suena el archivo analiza ESE audio; si no, el input.
    live.process (buffer);

    // Captura para coreografia por IA: vuelca el audio (sumado a mono) en el
    // buffer preasignado mientras el usuario reproduce la pista una vez.
    if (capturing.load (std::memory_order_relaxed))
    {
        const int n   = buffer.getNumSamples();
        const int nch = buffer.getNumChannels();
        int wp        = captureWritePos.load (std::memory_order_relaxed);

        if (nch > 0 && wp < captureCapacity)
        {
            const float invCh = 1.0f / (float) nch;
            for (int i = 0; i < n && wp < captureCapacity; ++i, ++wp)
            {
                float s = 0.0f;
                for (int ch = 0; ch < nch; ++ch)
                    s += buffer.getReadPointer (ch)[i];
                captureBuffer[(size_t) wp] = s * invCh;
            }
            captureWritePos.store (wp, std::memory_order_relaxed);

            if (wp >= captureCapacity)
                capturing.store (false, std::memory_order_relaxed);  // buffer lleno
        }
    }

    // Si es Connector, publica sus senales en el bus compartido (para el Main).
    if (auto* bus = activeBus.load (std::memory_order_relaxed))
    {
        bus->level.store     (live.getLevel(),          std::memory_order_relaxed);
        bus->bass.store      (live.getBass(),           std::memory_order_relaxed);
        bus->transient.store (live.getTransient(),      std::memory_order_relaxed);
        bus->transientCount.store (live.getTransientCount(), std::memory_order_relaxed);
        bus->lastUpdateMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    }

    // Passthrough: el plugin no altera el audio; solo lo escuchara para analizarlo.
    juce::ignoreUnused (buffer);
}

juce::AudioProcessorEditor* DmxVstAudioProcessor::createEditor()
{
    return new DmxVstAudioProcessorEditor (*this);
}

void DmxVstAudioProcessor::analyzeFile (const juce::File& file)
{
    lastAnalysis = analyzer.analyzeFile (file, onsetParams);
}

void DmxVstAudioProcessor::recomputeOnsets()
{
    if (lastAnalysis.valid)
        AudioAnalyzer::recomputeOnsets (lastAnalysis, onsetParams);
}

//==============================================================================
// Captura de audio del DAW + generacion de coreografia por IA

void DmxVstAudioProcessor::startCapture()
{
    capturing.store (false);   // detener antes de tocar el buffer

    captureSampleRate = preparedSampleRate;
    captureCapacity   = (int) (600.0 * captureSampleRate);   // 10 minutos

    if ((int) captureBuffer.size() != captureCapacity)
        captureBuffer.assign ((size_t) captureCapacity, 0.0f);

    captureWritePos.store (0);
    capturing.store (true);
}

void DmxVstAudioProcessor::stopCapture()
{
    capturing.store (false);
}

double DmxVstAudioProcessor::getCaptureSeconds() const noexcept
{
    if (captureSampleRate <= 0.0)
        return 0.0;
    return (double) captureWritePos.load() / captureSampleRate;
}

double DmxVstAudioProcessor::getCaptureFillFraction() const noexcept
{
    if (captureCapacity <= 0)
        return 0.0;
    return juce::jlimit (0.0, 1.0, (double) captureWritePos.load() / (double) captureCapacity);
}

bool DmxVstAudioProcessor::hasCapture() const noexcept
{
    // Al menos ~1 segundo de audio para que el analisis tenga sentido.
    return captureWritePos.load() > (int) (captureSampleRate * 1.0);
}

bool DmxVstAudioProcessor::generateAiShow()
{
    capturing.store (false);   // no analizar mientras se escribe

    const int n = captureWritePos.load();
    if (n < (int) (captureSampleRate * 1.0) || captureBuffer.empty())
        return false;

    // 1) Analisis DSP del audio capturado (forma de onda, bandas, onsets).
    AnalysisResult r = AudioAnalyzer::analyzeMono (captureBuffer.data(), n, captureSampleRate,
                                                   onsetParams, OfflineAnalyzer::kWaveformPoints);
    if (! r.valid)
        return false;

    r.fileName = "(captura del DAW)";
    lastAnalysis = r;   // muestra la forma de onda y los onsets capturados en el panel Analisis

    // 2) Construye el TrackAnalysis que entiende el motor de coreografia.
    TrackAnalysis a;
    a.lengthSeconds = r.lengthSeconds;
    a.energy        = OfflineAnalyzer::energyFromPeaks (r.peaks, OfflineAnalyzer::kEnergyPoints);
    a.transients    = r.onsetTimes;
    a.chroma        = OfflineAnalyzer::computeChroma (r);

    // Si el DAW da BPM, lo usamos para que los keyframes queden alineados al
    // compas del proyecto; si no, lo estimamos de los onsets.
    const double dawBpm = transport.hasPlayHead.load() ? transport.bpm.load() : 0.0;
    a.estimatedBpm = (dawBpm > 0.0) ? dawBpm : OfflineAnalyzer::estimateBpm (r.onsetTimes);
    a.valid = true;

    // 3) Genera el show usando los equipos actuales como rig.
    DmxShow show = ChoreographyEngine::generate (a, fixtures);
    if (! show.valid || show.fixtures.size() != fixtures.size())
        return false;

    // 4) Copia los keyframes/clips generados a los canales de los equipos reales.
    for (size_t fi = 0; fi < fixtures.size(); ++fi)
    {
        auto&       dst = fixtures[fi];
        const auto& src = show.fixtures[fi];
        const int   nc  = juce::jmin ((int) dst.channels.size(), (int) src.channels.size());
        for (int ci = 0; ci < nc; ++ci)
        {
            dst.channels[(size_t) ci].keyframes = src.channels[(size_t) ci].keyframes;
            dst.channels[(size_t) ci].clips     = src.channels[(size_t) ci].clips;
        }
    }

    // 5) Ajusta el transporte interno a la cancion (para previsualizar sin DAW).
    setInternalBpm (a.estimatedBpm);
    setInternalLength (a.lengthSeconds * a.estimatedBpm / 60.0);

    markFixturesDirty();
    return true;
}

void DmxVstAudioProcessor::addFixture (const Fixture& f)
{
    fixtures.push_back (f);
    updateHostDisplay();
}

void DmxVstAudioProcessor::updateFixture (int index, const Fixture& f)
{
    if (juce::isPositiveAndBelow (index, (int) fixtures.size()))
    {
        fixtures[(size_t) index] = f;
        updateHostDisplay();
    }
}

void DmxVstAudioProcessor::removeFixture (int index)
{
    if (juce::isPositiveAndBelow (index, (int) fixtures.size()))
    {
        fixtures.erase (fixtures.begin() + index);
        updateHostDisplay();
    }
}

void DmxVstAudioProcessor::removeRule (int index)
{
    if (juce::isPositiveAndBelow (index, (int) rules.size()))
    {
        rules.erase (rules.begin() + index);
        updateHostDisplay();
    }
}

void DmxVstAudioProcessor::setRole (Role r)
{
    role.store ((int) r);
    updateHubBinding();
    updateHostDisplay();
}

void DmxVstAudioProcessor::setBusName (const juce::String& name)
{
    busName = name;
    updateHubBinding();
    updateHostDisplay();
}

void DmxVstAudioProcessor::updateHubBinding()
{
    // Solo los Connectors publican en el hub.
    if (getRole() == Role::Connector && busName.isNotEmpty())
    {
        myBus = SharedHub::getInstance().getOrCreateBus (busName);
        activeBus.store (myBus.get());
    }
    else
    {
        activeBus.store (nullptr);
        myBus.reset();
    }
}

void DmxVstAudioProcessor::renderDmxFrame (double beats)
{
    // Partimos de un frame a cero y escribimos los canales en uso.
    for (auto& v : dmxBuffer)
        v.store (0, std::memory_order_relaxed);

    for (const auto& f : fixtures)
    {
        const int uni = f.universe;
        if (uni < 0 || uni >= kNumUniverses)
            continue;

        for (int ch = 0; ch < (int) f.channels.size(); ++ch)
        {
            const int address = f.startAddress + ch;   // 1..512
            if (address < 1 || address > kChannelsPerUni)
                continue;

            const auto& chan = f.channels[(size_t) ch];

            float value = evaluateChannel (chan.keyframes, chan.clips, beats);
            if (value < 0.0f)                 // sin automatizacion -> valor por defecto
                value = (float) chan.defaultValue;

            const int dmx = juce::jlimit (0, 255, juce::roundToInt (value));
            const int idx = uni * kChannelsPerUni + (address - 1);
            dmxBuffer[(size_t) idx].store ((juce::uint8) dmx, std::memory_order_relaxed);
        }
    }

    // Las reglas reactivas se aplican DESPUES de los keyframes (las sobreescriben).
    applyReactiveRules();
}

juce::uint8 DmxVstAudioProcessor::getDmxValue (int universe, int address) const noexcept
{
    if (universe < 0 || universe >= kNumUniverses)
        return 0;
    if (address < 1 || address > kChannelsPerUni)
        return 0;

    const int idx = universe * kChannelsPerUni + (address - 1);
    return dmxBuffer[(size_t) idx].load (std::memory_order_relaxed);
}

void DmxVstAudioProcessor::sendDmxToNetwork()
{
    const bool artOn  = artNet.isEnabled();
    const bool sacnOn = sacn.isEnabled();
    const bool entOn  = enttec.isEnabled();
    if (! artOn && ! sacnOn && ! entOn)
        return;

    // Determina que universos hay que emitir (los usados por los equipos).
    std::array<bool, kNumUniverses> used {};
    bool any = false;
    for (const auto& f : fixtures)
        if (f.universe >= 0 && f.universe < kNumUniverses)
        {
            used[(size_t) f.universe] = true;
            any = true;
        }

    if (! any)
        used[0] = true;   // sin equipos: emite al menos el universo 0

    juce::uint8 frame[kChannelsPerUni];
    for (int u = 0; u < kNumUniverses; ++u)
    {
        if (! used[(size_t) u])
            continue;

        for (int a = 0; a < kChannelsPerUni; ++a)
            frame[a] = dmxBuffer[(size_t) (u * kChannelsPerUni + a)].load (std::memory_order_relaxed);

        if (artOn)
            artNet.sendUniverse (u, frame, kChannelsPerUni);

        // sACN numera universos desde 1; mapeamos el universo interno u -> u + 1.
        if (sacnOn)
            sacn.sendUniverse (u + 1, frame, kChannelsPerUni);
    }

    // Enttec USB Pro: un dispositivo emite UN universo (el elegido en su config).
    if (entOn)
    {
        const int eu = juce::jlimit (0, kNumUniverses - 1, enttec.getUniverse());
        juce::uint8 euFrame[kChannelsPerUni];
        for (int a = 0; a < kChannelsPerUni; ++a)
            euFrame[a] = dmxBuffer[(size_t) (eu * kChannelsPerUni + a)].load (std::memory_order_relaxed);
        enttec.sendUniverse (euFrame, kChannelsPerUni);
    }
}

void DmxVstAudioProcessor::applyReactiveRules()
{
    if (rules.empty())
        return;

    colorCycleIndex.resize (rules.size(), 0);
    lastTransientPerRule.resize (rules.size(), 0);

    auto writeChannel = [this] (int universe, int address, int value)
    {
        if (universe < 0 || universe >= kNumUniverses) return;
        if (address < 1 || address > kChannelsPerUni)  return;
        const int idx = universe * kChannelsPerUni + (address - 1);
        dmxBuffer[(size_t) idx].store ((juce::uint8) juce::jlimit (0, 255, value),
                                       std::memory_order_relaxed);
    };

    for (size_t ri = 0; ri < rules.size(); ++ri)
    {
        const auto& r = rules[ri];
        if (! r.enabled)
            continue;
        if (! juce::isPositiveAndBelow (r.fixtureIndex, (int) fixtures.size()))
            continue;

        const auto& f = fixtures[(size_t) r.fixtureIndex];

        // Fuente de senales: bus (pista de un Connector) o audio propio.
        float level = 0.0f, bass = 0.0f, transient = 0.0f;
        int   transientCount = 0;

        if (r.busName.isNotEmpty())
        {
            if (auto bus = SharedHub::getInstance().findBus (r.busName))
            {
                level          = bus->level.load();
                bass           = bus->bass.load();
                transient      = bus->transient.load();
                transientCount = bus->transientCount.load();
            }
            // Bus no disponible -> senales en cero (la luz queda apagada).
        }
        else
        {
            level          = live.getLevel();
            bass           = live.getBass();
            transient      = live.getTransient();
            transientCount = live.getTransientCount();
        }

        const bool newTransient = (transientCount != lastTransientPerRule[ri]);
        lastTransientPerRule[ri] = transientCount;

        float sig = 0.0f;
        switch (r.source)
        {
            case ReactiveRule::Source::Level:     sig = level;     break;
            case ReactiveRule::Source::Bass:      sig = bass;      break;
            case ReactiveRule::Source::Transient: sig = transient; break;
        }

        if (r.colorMode)
        {
            // En cada transitorio nuevo avanza la paleta y fija RGB del equipo.
            if (newTransient)
                ++colorCycleIndex[ri];

            const auto col = reactivePalette (colorCycleIndex[ri]);

            // Atenuar el color por la senal seleccionada (p.ej. transitorio que decae).
            const float amt = (r.source == ReactiveRule::Source::Transient) ? 1.0f : sig;

            for (int ch = 0; ch < (int) f.channels.size(); ++ch)
            {
                const auto type = f.channels[(size_t) ch].type;
                int v = -1;
                if (type == ChannelType::Red)   v = (int) (col.getRed()   * amt);
                if (type == ChannelType::Green) v = (int) (col.getGreen() * amt);
                if (type == ChannelType::Blue)  v = (int) (col.getBlue()  * amt);
                if (v >= 0)
                    writeChannel (f.universe, f.startAddress + ch, v);
            }
        }
        else
        {
            const int value = r.outLow + (int) (sig * (r.outHigh - r.outLow));
            writeChannel (f.universe, f.startAddress + r.channelIndex, value);
        }
    }
}

void DmxVstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("LuxSyncState");
    juce::ValueTree fx ("Fixtures");
    for (const auto& f : fixtures)
        fx.appendChild (f.toValueTree(), nullptr);
    state.appendChild (fx, nullptr);

    juce::ValueTree rl ("Rules");
    for (const auto& r : rules)
        rl.appendChild (r.toValueTree(), nullptr);
    state.appendChild (rl, nullptr);

    state.setProperty ("role", role.load(), nullptr);
    state.setProperty ("busName", busName, nullptr);

    state.setProperty ("artNetEnabled",   artNet.isEnabled(),   nullptr);
    state.setProperty ("artNetBroadcast", artNet.isBroadcast(), nullptr);
    state.setProperty ("artNetTarget",    artNet.getTargetIp(), nullptr);
    state.setProperty ("artNetPort",      artNet.getPort(),     nullptr);

    state.setProperty ("sacnEnabled",   sacn.isEnabled(),   nullptr);
    state.setProperty ("sacnMulticast", sacn.isMulticast(), nullptr);
    state.setProperty ("sacnTarget",    sacn.getTargetIp(), nullptr);
    state.setProperty ("sacnPriority",  sacn.getPriority(), nullptr);

    state.setProperty ("netInterface",  artNet.getLocalInterface(), nullptr);

    state.setProperty ("enttecEnabled",  enttec.isEnabled(),   nullptr);
    state.setProperty ("enttecPort",     enttec.getPort(),     nullptr);
    state.setProperty ("enttecUniverse", enttec.getUniverse(), nullptr);

    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void DmxVstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid())
        return;

    fixtures.clear();
    const auto fx = state.getChildWithName ("Fixtures");
    for (int i = 0; i < fx.getNumChildren(); ++i)
        fixtures.push_back (Fixture::fromValueTree (fx.getChild (i)));

    rules.clear();
    const auto rl = state.getChildWithName ("Rules");
    for (int i = 0; i < rl.getNumChildren(); ++i)
        rules.push_back (ReactiveRule::fromValueTree (rl.getChild (i)));

    role.store ((int) state.getProperty ("role", (int) Role::Main));
    busName = state.getProperty ("busName", "Pista 1").toString();
    updateHubBinding();

    artNet.setBroadcast ((bool) state.getProperty ("artNetBroadcast", true));
    artNet.setTargetIp  (state.getProperty ("artNetTarget", "255.255.255.255").toString());
    artNet.setPort      ((int)  state.getProperty ("artNetPort", 6454));
    artNet.setEnabled   ((bool) state.getProperty ("artNetEnabled", false));

    sacn.setMulticast ((bool) state.getProperty ("sacnMulticast", true));
    sacn.setTargetIp  (state.getProperty ("sacnTarget", "").toString());
    sacn.setPriority  ((int)  state.getProperty ("sacnPriority", 100));
    sacn.setEnabled   ((bool) state.getProperty ("sacnEnabled", false));

    {
        const auto iface = state.getProperty ("netInterface", "").toString();
        artNet.setLocalInterface (iface);
        sacn.setLocalInterface (iface);
    }

    enttec.setUniverse ((int) state.getProperty ("enttecUniverse", 0));
    enttec.setPort     (state.getProperty ("enttecPort", "").toString());
    enttec.setEnabled  ((bool) state.getProperty ("enttecEnabled", false));
}

// Punto de entrada que el host usa para crear el plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DmxVstAudioProcessor();
}
