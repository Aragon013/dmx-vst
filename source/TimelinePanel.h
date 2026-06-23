#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
    Editor tipo timeline / piano-roll por equipo.

    Muestra una pista por canal del equipo seleccionado y permite editar a mano
    los keyframes de cada canal (valor 0..255 en el tiempo, en compases/beats
    sincronizados al BPM). Interpolacion lineal o por pasos (step).

    Interaccion:
      - Click en zona vacia: crea un keyframe (y permite arrastrarlo).
      - Arrastrar keyframe: mueve tiempo (X) y valor (Y).
      - Doble click sobre keyframe: lo borra.
      - Click derecho sobre keyframe: alterna rampa/step.

    Las lineas ambar tenues son los onsets detectados (guias para colocar triggers).
*/
class TimelinePanel : public juce::Component,
                      private juce::Timer,
                      private juce::ScrollBar::Listener,
                      private juce::ChangeListener
{
public:
    explicit TimelinePanel (DmxVstAudioProcessor&);
    ~TimelinePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void refreshFixtures();
    void syncInternalTransport();   // pasa BPM y longitud al transporte interno

    int    currentFixtureIndex() const;
    double totalBeats() const;
    double currentBpm() const;
    int    beatsPerBar() const;
    juce::Rectangle<int> getRulerBounds() const;
    juce::Rectangle<int> getTracksBounds() const;       // toda la zona de pistas (sin etiquetas)
    juce::Rectangle<int> getAiBarBounds() const;        // franja de controles de IA
    juce::Rectangle<int> getRowBounds (int channelIndex, int numChannels) const;
    juce::Rectangle<int> getSwatchBounds (int channelIndex, int numChannels) const; // muestra de color en la etiqueta

    // Selector de color del canal
    void   changeListenerCallback (juce::ChangeBroadcaster*) override;
    void   openColourPicker (int channelIndex);

    // Zoom / desplazamiento horizontal
    void   scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void   updateScrollBar();
    void   clampScroll();
    double pixelsPerBeat() const;
    double visibleBeats() const;

    float  beatToX (double beats) const;
    double xToBeat (float x) const;
    float  valueToY (float value, juce::Rectangle<int> row) const;
    float  yToValue (float y, juce::Rectangle<int> row) const;

    // Localiza el keyframe (canal, indice) bajo el punto, o devuelve false.
    bool   hitTestKeyframe (juce::Point<int> p, int& channelOut, int& kfOut) const;

    // --- Clips de efecto (LFO) ---
    juce::Rectangle<float> getClipBounds (int channelIndex, int clipIdx, int numChannels) const;
    // Localiza el clip bajo el punto; edgeOut: 0=cuerpo, 1=borde izq, 2=borde der.
    bool   hitTestClip (juce::Point<int> p, int& channelOut, int& clipOut, int& edgeOut) const;
    void   openClipEditor (int channelIndex, int clipIdx);

    // --- Deshacer / rehacer y portapapeles de keyframes ---
    void   pushUndoSnapshot();          // guarda el estado actual antes de editar
    void   doUndo();
    void   doRedo();
    void   copyChannel (int channelIndex);                 // copia los keyframes del canal
    void   pasteToChannel (int channelIndex, double atBeat);
    int    targetChannel() const;       // canal bajo el raton (o el ultimo usado)

    DmxVstAudioProcessor& processorRef;

    juce::Label    fixtureLabel;
    juce::ComboBox fixtureCombo;
    juce::Label    bpmLabel;
    juce::Slider   bpmSlider;
    juce::Label    barsLabel;
    juce::Slider   barsSlider;
    juce::Label    beatsLabel;
    juce::Slider   beatsSlider;
    juce::ToggleButton snapButton { "Snap a beat" };
    juce::ToggleButton clipModeButton { "Efectos" };

    juce::TextButton   playButton   { "Play" };
    juce::TextButton   stopButton   { "Stop" };
    juce::TextButton   rewindButton { "|<" };
    juce::ToggleButton loopButton   { "Loop" };

    // --- Coreografia por IA (captura del DAW + generacion) ---
    juce::TextButton captureButton  { "Capturar pista" };
    juce::TextButton generateButton { "Generar IA" };
    juce::Label      aiStatusLabel;
    void updateAiUi();

    juce::ScrollBar hScroll { false };   // barra de scroll horizontal
    double zoom        = 1.0;            // 1.0 = todos los compases visibles
    double scrollBeats = 0.0;            // beat del borde izquierdo

    int   lastFixtureCount = -1;

    // Estado de arrastre
    bool  dragging       = false;
    int   dragChannel    = -1;
    int   dragKeyframe   = -1;
    bool  scrubbing      = false;   // arrastrando el playhead desde la regla

    // Estado de arrastre de clips de efecto
    bool   draggingClip = false;
    int    clipChannel  = -1;
    int    clipIndex    = -1;
    int    clipDragMode = 0;        // 0=mover, 1=redimensionar izq, 2=redimensionar der
    double clipGrabOffset = 0.0;    // offset (beats) entre el cursor y el inicio del clip al mover

    // Estado de hover (para resaltar la fila y la posicion en la regla)
    juce::Point<int> hoverPos { -1, -1 };
    int   hoverRow       = -1;

    // Canal cuyo color se esta editando con el selector
    int   colourEditChannel = -1;

    // Deshacer/rehacer (instantaneas de todos los equipos) + portapapeles
    std::vector<std::vector<Fixture>> undoStack;
    std::vector<std::vector<Fixture>> redoStack;
    std::vector<Keyframe>             clipboard;
    double                            clipboardBase = 0.0;
    static constexpr int kMaxUndo = 60;

    static constexpr int kLabelWidth = 130;
    static constexpr int kRulerHeight = 22;
    static constexpr int kScrollBarHeight = 12;
    static constexpr int kAiBarHeight = 32;   // segunda franja: controles de IA

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelinePanel)
};
