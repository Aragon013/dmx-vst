#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include "AudioAnalyzer.h"
#include "LiveAnalyzer.h"
#include "FixtureModel.h"
#include "Reactive.h"
#include "SharedHub.h"
#include "ArtNetSender.h"
#include "SacnSender.h"
#include "EnttecSender.h"

/**
    Instantanea del estado de transporte del DAW (host).

    Se rellena en processBlock (hilo de audio) y se lee desde la UI (hilo
    de mensajes). Cada campo es atomico para evitar data races sin locks.
*/
struct TransportState
{
    std::atomic<bool>   isPlaying   { false };
    std::atomic<bool>   hasPlayHead { false };
    std::atomic<double> bpm         { 0.0 };
    std::atomic<double> timeSeconds { 0.0 };
    std::atomic<double> ppqPosition { 0.0 };   // posicion en quarter notes
    std::atomic<int>    timeSigNum  { 0 };
    std::atomic<int>    timeSigDen  { 0 };
};

/**
    Procesador principal del plugin.

    En esta primera fase es un passthrough de audio (no modifica el sonido).
    El audio entra solo para ANALIZARLO; la salida util del plugin sera DMX,
    no audio. Por eso processBlock no toca el buffer todavia.
*/
class DmxVstAudioProcessor : public juce::AudioProcessor,
                             private juce::HighResolutionTimer,
                             private juce::Timer
{
public:
    DmxVstAudioProcessor();
    ~DmxVstAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Estado de transporte del DAW, actualizado cada bloque de audio. */
    const TransportState& getTransportState() const noexcept { return transport; }

    /** Analiza un archivo de audio (offline) y guarda el resultado. */
    void analyzeFile (const juce::File& file);

    /** Ultimo resultado de analisis (puede ser invalido si aun no se hizo). */
    const AnalysisResult& getLastAnalysis() const noexcept { return lastAnalysis; }

    /** Parametros de deteccion de onsets (ajustables desde la UI). */
    OnsetParams& getOnsetParams() noexcept { return onsetParams; }

    /** Re-detecta onsets con los parametros actuales (sin recargar el archivo). */
    void recomputeOnsets();

    //==============================================================================
    // Coreografia por IA dentro del DAW: captura el audio de la pista mientras
    // suena, lo analiza y genera automaticamente los keyframes del show. El usuario
    // puede editarlos despues a mano en el timeline (mismo modelo, una sola pista).

    /** Empieza a capturar el audio que pasa por el plugin (hasta ~10 min). */
    void startCapture();
    /** Detiene la captura. */
    void stopCapture();
    bool isCapturing() const noexcept { return capturing.load(); }
    /** Segundos de audio capturados hasta ahora. */
    double getCaptureSeconds() const noexcept;
    /** Fraccion 0..1 del buffer de captura usada (para una barra de progreso). */
    double getCaptureFillFraction() const noexcept;
    /** Hay audio capturado suficiente para generar. */
    bool hasCapture() const noexcept;
    /** Analiza la captura y genera el show (rellena keyframes de los equipos).
        Devuelve true si genero algo. Se llama desde el hilo de mensajes. */
    bool generateAiShow();

    /** Equipos (fixtures) del proyecto. */
    const std::vector<Fixture>& getFixtures() const noexcept { return fixtures; }
    std::vector<Fixture>& getFixtures() noexcept { return fixtures; }
    void addFixture (const Fixture& f);
    void updateFixture (int index, const Fixture& f);
    void removeFixture (int index);

    /** Marca el proyecto como modificado (tras editar keyframes desde el timeline). */
    void markFixturesDirty() { updateHostDisplay(); }

    //==============================================================================
    // Motor de playback / salida DMX

    static constexpr int kNumUniverses    = 8;
    static constexpr int kChannelsPerUni  = 512;

    /** Evalua los keyframes de todos los equipos en el instante dado (beats) y
        rellena el buffer DMX. Se llama desde el hilo de mensajes (timer de UI). */
    void renderDmxFrame (double beats);

    /** Valor DMX actual de un canal (universo 0..kNumUniverses-1, address 1..512). */
    juce::uint8 getDmxValue (int universe, int address) const noexcept;

    /** Envia el buffer DMX actual por la red (Art-Net). Solo emite si esta activado.
        Se llama tras renderDmxFrame, al mismo ritmo (~44 Hz). */
    void sendDmxToNetwork();

    // --- Configuracion de salida Art-Net (UI + persistencia) ---
    void setArtNetEnabled (bool b)              { artNet.setEnabled (b); }
    bool isArtNetEnabled() const                { return artNet.isEnabled(); }
    void setArtNetBroadcast (bool b)            { artNet.setBroadcast (b); }
    bool isArtNetBroadcast() const              { return artNet.isBroadcast(); }
    void setArtNetTarget (const juce::String& ip){ artNet.setTargetIp (ip); }
    juce::String getArtNetTarget() const        { return artNet.getTargetIp(); }
    void setArtNetPort (int p)                  { artNet.setPort (p); }
    int  getArtNetPort() const                  { return artNet.getPort(); }

    // --- Configuracion de salida sACN E1.31 (UI + persistencia) ---
    void setSacnEnabled (bool b)                { sacn.setEnabled (b); }
    bool isSacnEnabled() const                  { return sacn.isEnabled(); }
    void setSacnMulticast (bool b)              { sacn.setMulticast (b); }
    bool isSacnMulticast() const                { return sacn.isMulticast(); }
    void setSacnTarget (const juce::String& ip) { sacn.setTargetIp (ip); }
    juce::String getSacnTarget() const          { return sacn.getTargetIp(); }
    void setSacnPriority (int p)                { sacn.setPriority (p); }
    int  getSacnPriority() const                { return sacn.getPriority(); }

    // --- Interfaz de red (NIC) de salida, compartida por Art-Net y sACN ---
    void setNetInterface (const juce::String& ip) { artNet.setLocalInterface (ip); sacn.setLocalInterface (ip); }
    juce::String getNetInterface() const          { return artNet.getLocalInterface(); }

    // --- Configuracion de salida Enttec USB Pro (UI + persistencia) ---
    void setEnttecEnabled (bool b)              { enttec.setEnabled (b); }
    bool isEnttecEnabled() const                { return enttec.isEnabled(); }
    void setEnttecPort (const juce::String& p)  { enttec.setPort (p); }
    juce::String getEnttecPort() const          { return enttec.getPort(); }
    void setEnttecUniverse (int u)              { enttec.setUniverse (u); }
    int  getEnttecUniverse() const              { return enttec.getUniverse(); }
    bool isEnttecConnected() const              { return enttec.isConnected(); }
    juce::StringArray getSerialPorts() const    { return EnttecSender::getAvailablePorts(); }

    /** Posicion de reproduccion del host en beats (ppq). Valida solo si hay host. */
    double getHostBeats() const noexcept { return transport.ppqPosition.load(); }

    //==============================================================================
    // Transporte interno (para Standalone, cuando no hay host que reproduzca).
    // Se avanza en el hilo de audio (processBlock) de forma precisa por muestras.

    void   transportPlay()   noexcept;
    void   transportStop()   noexcept;
    void   transportToggle() noexcept;
    void   transportRewind() noexcept;

    bool   isInternalPlaying() const noexcept { return internalPlaying.load(); }
    void   setInternalBeats (double beats) noexcept { internalBeats.store (juce::jmax (0.0, beats)); }
    void   setInternalBpm (double bpm) noexcept     { internalBpm.store (bpm); }
    void   setInternalLength (double beats) noexcept{ internalLength.store (beats); }
    void   setInternalLoop (bool shouldLoop) noexcept;
    bool   getInternalLoop() const noexcept         { return internalLoop.load(); }

    /** Mueve la posicion de reproduccion a 'beats' (mueve tambien el audio si lo hay). */
    void   seekToBeats (double beats) noexcept;

    //==============================================================================
    // Reproduccion de audio (modo Standalone: suena la cancion y mueve el playhead).

    /** Carga un archivo de audio para reproducirlo en sincronia con el transporte. */
    bool   loadAudioForPlayback (const juce::File& file);
    bool   hasPlaybackAudio() const noexcept { return hasAudioFile.load(); }
    double getAudioLengthSeconds() const noexcept { return audioLengthSec.load(); }
    juce::String getAudioFileName() const { return audioFileName; }

    /** Posicion de reproduccion del audio cargado, en segundos. */
    double getPlaybackSeconds() noexcept;

    /** Mueve la reproduccion a 'seconds' del audio cargado. */
    void   seekToSeconds (double seconds) noexcept;

    /** True si hay un host que controla el transporte (DAW). */
    bool   hostIsActive() const noexcept { return transport.hasPlayHead.load(); }

    /** Posicion de reproduccion unificada (host si lo hay, si no la interna). */
    double getPlayheadBeats() const noexcept
    {
        return transport.hasPlayHead.load() ? transport.ppqPosition.load()
                                            : internalBeats.load();
    }

    /** True si algo esta reproduciendo (host o transporte interno). */
    bool   isPlayingNow() const noexcept
    {
        return transport.hasPlayHead.load() ? transport.isPlaying.load()
                                            : internalPlaying.load();
    }

    //==============================================================================
    // Analisis en vivo (tiempo real) + reglas reactivas

    float getLiveLevel()     const noexcept { return live.getLevel(); }
    float getLiveBass()      const noexcept { return live.getBass(); }
    float getLiveTransient() const noexcept { return live.getTransient(); }
    LiveAnalyzer& getLiveAnalyzer() noexcept { return live; }

    const std::vector<ReactiveRule>& getRules() const noexcept { return rules; }
    std::vector<ReactiveRule>& getRules() noexcept { return rules; }
    void addRule (const ReactiveRule& r) { rules.push_back (r); updateHostDisplay(); }
    void removeRule (int index);

    //==============================================================================
    // Rol de la instancia: Main (controla DMX) o Connector (analiza una pista y publica).

    enum class Role { Main, Connector };

    Role getRole() const noexcept { return (Role) role.load(); }
    void setRole (Role r);

    juce::String getBusName() const { return busName; }
    void setBusName (const juce::String& name);

    /** Nombres de buses (pistas) vivos publicados por los Connectors. */
    juce::StringArray getAvailableBuses() { return SharedHub::getInstance().getBusNames(); }

private:
    TransportState transport;
    AudioAnalyzer  analyzer;
    AnalysisResult lastAnalysis;
    OnsetParams    onsetParams;
    LiveAnalyzer   live;
    std::vector<Fixture> fixtures;
    std::vector<ReactiveRule> rules;

    // Transporte interno (Standalone / sin host).
    std::atomic<bool>   internalPlaying { false };
    std::atomic<double> internalBeats   { 0.0 };
    std::atomic<double> internalBpm     { 120.0 };
    std::atomic<double> internalLength  { 16.0 };  // beats totales (0 = sin loop)
    std::atomic<bool>   internalLoop    { true };
    double              lastTickMs      { 0.0 };   // reloj de pared para el avance interno

    void hiResTimerCallback() override;            // avanza el transporte interno
    void timerCallback() override;                 // render + salida DMX (hilo de mensajes)

    // Reproduccion de audio (suena el archivo y mueve el transporte en Standalone).
    juce::AudioFormatManager                       playbackFormatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource                     transportSource;
    juce::CriticalSection                          audioLock;
    std::atomic<bool>   hasAudioFile   { false };
    std::atomic<double> audioLengthSec { 0.0 };
    juce::String        audioFileName;
    double              preparedSampleRate { 44100.0 };
    int                 preparedBlockSize  { 512 };

    // Captura de audio del DAW para generar coreografia por IA (buffer mono
    // preasignado; processBlock solo escribe, sin reservar memoria).
    std::vector<float>  captureBuffer;            // mono, asignado al iniciar captura
    std::atomic<int>    captureWritePos { 0 };
    std::atomic<bool>   capturing       { false };
    int                 captureCapacity { 0 };    // muestras maximas (10 min)
    double              captureSampleRate { 44100.0 };

    // Rol e identidad de pista (Connector).
    std::atomic<int> role { (int) Role::Main };
    juce::String     busName { "Pista 1" };
    std::shared_ptr<BusSignals> myBus;            // bus propio si es Connector (mantiene vida)
    std::atomic<BusSignals*>    activeBus { nullptr };  // leido en processBlock

    // Estado del ciclo de color por regla (indice de paleta + ultimo contador de transitorio).
    int lastTransientCount = 0;
    std::vector<int> colorCycleIndex;
    std::vector<int> lastTransientPerRule;

    std::array<std::atomic<juce::uint8>, kNumUniverses * kChannelsPerUni> dmxBuffer {};

    ArtNetSender artNet;   // salida DMX por red (Art-Net)
    SacnSender   sacn;     // salida DMX por red (sACN E1.31)
    EnttecSender enttec;   // salida DMX por hardware (Enttec USB Pro)

    void applyReactiveRules();
    void updateHubBinding();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxVstAudioProcessor)
};
