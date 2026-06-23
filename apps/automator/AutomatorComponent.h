#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PlaylistManager.h"
#include "AudioPlayer.h"
#include "PlaybackEngine.h"
#include "DmxPreview.h"
#include "StageVisualizer.h"
#include "SectionBar.h"
#include "StemAssignPanel.h"
#include "CustomFixturePanel.h"
#include "PreferredColorsPanel.h"
#include "SongPropertiesPanel.h"
#include "ChoreoPanel.h"
#include "SongChoreoPanel.h"
#include "SequenceEditorPanel.h"
#include "ManualShowEditor.h"
#include "PianoRollPanel.h"
#include "PreferencesPanel.h"
#include "MusicLibrary.h"
#include "LibraryPanel.h"
#include "../../source/LuxLookAndFeel.h"

/**
    Componente principal del AI Automator (Fase 1).

    Muestra la playlist (lista de temas con duracion y estado), botones para
    anadir / quitar / reordenar y un transporte basico (Play/Pause/Stop) que
    reproduce el tema seleccionado. Al terminar un tema, encadena el siguiente.
*/
class AutomatorComponent : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::ChangeListener,
                           private juce::Timer,
                           public  juce::MenuBarModel
{
public:
    AutomatorComponent();
    ~AutomatorComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // MenuBarModel (barra de menus tipo DAW: Archivo / Edicion / Opciones / Acerca de).
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int lastRowSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    // ChangeListener (PlaylistManager)
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Timer (refresca el transporte / encadena temas)
    void timerCallback() override;

    void addFilesDialog();
    void removeSelected();
    void moveSelected (int delta);
    void playSelected();
    void playTrack (int index);
    void playNext();
    void togglePlayPause();

    // Cola de reproduccion (sigue el orden visible del Reproductor).
    void startQueue (const juce::Array<juce::File>& files, int startIndex);
    void rebuildPlayOrder (int startFileIndex);
    void playOrderPos();
    void stopPlayback();
    void updateActiveShow();
    void updateViewMode();
    void setTab (int tab);
    void rebuildIdleShow();
    void updateTabVisibility();
    void showAboutDialog();

    static juce::String formatTime (double seconds);

    LuxLookAndFeel lux;

    juce::MenuBarComponent menuBar;

    PlaylistManager playlist;
    MusicLibrary    musicLibrary;
    AudioPlayer     player;
    PlaybackEngine  engine;

    DmxShow         idleShow;          // rig "en reposo" para mostrar el escenario sin tema activo

    int currentlyPlaying = -1;
    double animPhase = 0.0;       // fase para la barra de progreso indeterminada

    // Cola de reproduccion derivada del orden visible del Reproductor.
    juce::Array<juce::File> playQueue;   // archivos en el orden mostrado
    std::vector<int>        playOrder;   // permutacion de indices de playQueue (orden real)
    int                     orderPos = -1;  // posicion dentro de playOrder
    bool                    shuffleMode = false;
    bool                    repeatMode  = false;
    juce::Random            shuffleRng;

    juce::Label    titleLabel;
    juce::ListBox  list { "playlist", this };

    juce::TextButton addButton    { "Anadir..." };
    juce::TextButton removeButton  { "Quitar" };
    juce::TextButton upButton      { "Subir" };
    juce::TextButton downButton    { "Bajar" };

    juce::TextButton playButton    { "Play" };
    juce::TextButton stopButton    { "Stop" };
    juce::TextButton blackoutButton { "BLACKOUT" };
    juce::Slider     positionSlider;
    SectionBar       sectionBar;
    juce::Label      timeLabel;
    juce::Label      nowPlayingLabel;

    DmxPreview       preview;
    StageVisualizer  stage;
    juce::TextButton viewButton    { "Escenario" };
    juce::TextButton perspectiveButton { "2.5D" };
    bool             showStage = true;
    juce::ToggleButton artNetButton { "Art-Net" };
    juce::Label      artNetIpLabel;
    juce::TextEditor artNetIp;

    juce::ToggleButton sacnButton { "sACN" };
    juce::Label        sacnIpLabel;
    juce::TextEditor   sacnIp;

    juce::Label        netIfaceLabel;
    juce::ComboBox     netIfaceCombo;
    juce::StringArray  netIfaceIps;        // IPs locales por indice del combo (1=Auto vacio)
    void rescanNetInterfaces();
    juce::ToggleButton enttecButton { "Enttec" };
    juce::ComboBox     enttecPortCombo;
    juce::TextButton   enttecRefreshButton { "Puertos" };
    juce::Label        enttecUniLabel;
    juce::Slider       enttecUniSlider;
    juce::ComboBox     enttecProtocolCombo;
    juce::Label        enttecStatusLabel;
    void rescanEnttecPorts();
    void refreshEnttecUi();
    void syncDmxOutputsFromSession();

    juce::Label      stemStatusLabel;
    juce::Label      styleLabel;
    juce::ComboBox   styleCombo;

    // Offsets de brillo de la barra de pixeles (suelo de color y de blanco).
    juce::Label  colorOffsetLabel;
    juce::Slider colorOffsetKnob;
    juce::Label  whiteOffsetLabel;
    juce::Slider whiteOffsetKnob;

    // Re-entrenar (re-separar stems) del tema seleccionado.
    juce::TextButton retrainButton { "Generar Stems" };
    void retrainSelected();
    void updateRetrainEnabled();

    // Colores preferidos del tema seleccionado.
    juce::TextButton colorsButton { "Colores..." };
    void editPreferredColors();
    void editPreferredColorsForFile (const juce::File& f);

    // Ventana de propiedades del tema (colores + coreografia + analisis).
    void showSongProperties (const juce::File& f);

    // Coreografias del tema seleccionado (elegir de la libreria, por equipo).
    juce::TextButton songChoreoButton { "Coreografias..." };
    void editSongChoreosForFile (const juce::File& f,
                                 const juce::StringArray& playlistPaths,
                                 const juce::String& playlistName);

    // Piano-roll manual: edita la coreografia manual de un tema (indice de playlist).
    void openManualEditor (int playlistIndex);
    void launchManualEditorWindow (int playlistIndex);

    // Ventana aparte con el escenario (para verlo mientras se edita el piano roll).
    void openStageWindow();
    void closeStageWindow();
    std::unique_ptr<juce::DocumentWindow> stageWindow;
    StageVisualizer* popoutStage = nullptr;    // escenario de la ventana aparte (lo posee stageWindow)

    // Proyectos .lux (guardar / abrir / recientes).
    void newProjectAction();
    void openProjectAction();
    void openProjectPath (const juce::File& file);
    void saveProjectAction();
    void saveProjectAsAction();
    void updateWindowTitle();

    // Preferencias de la app.
    void showPreferences();

    int trackIndexForFile (const juce::File& f) const;

    juce::TextButton audioButton { "Audio..." };
    void showAudioSettings();

    // Prueba de salida: fuerza todo el rig a tope para verificar patch/cableado.
    juce::TextButton testButton { "Probar" };
    bool             testActive = false;
    void toggleTest();
    std::vector<PlaybackEngine::Universe> buildTestFrame() const;

    // Pestanas: 0 = Reproductor (biblioteca), 1 = Escenario, 2 = Stems, 3 = Equipos Custom, 4 = Creador, 5 = Salida DMX, 6 = Piano roll.
    int currentTab = 0;
    juce::TextButton tabPlayerButton { "Reproductor" };
    juce::TextButton tabStageButton  { "Escenario" };
    juce::TextButton tabStemsButton  { "Stems" };
    juce::TextButton tabCustomButton { "Equipos Custom" };
    juce::TextButton tabCreatorButton { "Creador" };
    juce::TextButton tabPianoButton  { "Piano roll" };
    juce::TextButton tabOutputButton { "Salida DMX" };

    // Modo del Creador: secuencias manuales o coreografias parametricas.
    juce::TextButton creatorManualButton { "Manual" };
    juce::TextButton creatorParamButton  { "Parametrico" };
    bool             creatorParametric = false;
    void setCreatorMode (bool parametric);

    std::unique_ptr<StemAssignPanel>   stemPanel;
    std::unique_ptr<CustomFixturePanel> customPanel;
    std::unique_ptr<ChoreoPanel>        choreoPanel;
    std::unique_ptr<SequenceEditorPanel> seqEditor;
    std::unique_ptr<LibraryPanel>       libraryPanel;
    std::unique_ptr<PianoRollPanel>     pianoPanel;
    bool                                creatorWasTesting = false;

    std::unique_ptr<juce::FileChooser> chooser;
    bool draggingPosition = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomatorComponent)
};
