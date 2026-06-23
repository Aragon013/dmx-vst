#include "AutomatorComponent.h"
#include <algorithm>

using P = LuxLookAndFeel::Palette;

namespace
{
    juce::String extractComPortName (const juce::String& text)
    {
        const auto upper = text.toUpperCase();
        for (int pos = 0; pos + 3 <= upper.length(); ++pos)
        {
            if (upper[pos] != 'C' || upper[pos + 1] != 'O' || upper[pos + 2] != 'M')
                continue;

            int end = pos + 3;
            while (end < upper.length() && upper[end] >= '0' && upper[end] <= '9')
                ++end;

            if (end > pos + 3)
                return "COM" + upper.substring (pos + 3, end);
        }
        return text.trim();
    }

    int preferredPortScore (const juce::String& label)
    {
        const auto u = label.toUpperCase();
        if (u.contains ("ENTTEC"))     return 5;
        if (u.contains ("DMX"))        return 4;
        if (u.contains ("FTDI"))       return 3;
        if (u.contains ("USB SERIAL")) return 2;
        if (u.contains ("USB"))        return 1;
        return 0;
    }
}

AutomatorComponent::AutomatorComponent()
{
    setLookAndFeel (&lux);
    setWantsKeyboardFocus (true);   // recibe la barra espaciadora (play/pausa)
    // Barra de menus tipo DAW (Archivo / Edicion / Opciones / Acerca de).
    menuBar.setModel (this);
    addAndMakeVisible (menuBar);

    titleLabel.setText ("LuxSync  ·  AI Automator", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
    addAndMakeVisible (titleLabel);

    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (P::surface));
    list.setColour (juce::ListBox::outlineColourId, juce::Colour (P::line));
    list.setOutlineThickness (1);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    auto setupButton = [this] (juce::TextButton& b)
    {
        b.setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    };

    setupButton (addButton);
    setupButton (removeButton);
    setupButton (upButton);
    setupButton (downButton);
    setupButton (playButton);
    setupButton (stopButton);
    setupButton (blackoutButton);

    addButton.onClick    = [this] { addFilesDialog(); };
    removeButton.onClick  = [this] { removeSelected(); };
    upButton.onClick      = [this] { moveSelected (-1); };
    downButton.onClick    = [this] { moveSelected (+1); };

    playButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a6a2a));
    playButton.onClick = [this] { togglePlayPause(); };
    stopButton.onClick = [this] { stopPlayback(); };

    blackoutButton.setClickingTogglesState (true);
    blackoutButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a1014));
    blackoutButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd11a2a));
    blackoutButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::textMid));
    blackoutButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    blackoutButton.setTooltip ("Apagon de emergencia: pone TODOS los canales a 0 al instante. Atajo: B o Esc. Vuelve a pulsar para reanudar.");
    blackoutButton.onClick = [this]
    {
        engine.setBlackout (blackoutButton.getToggleState());
        repaint();
    };

    positionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    positionSlider.setRange (0.0, 1.0, 0.0);
    positionSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    positionSlider.onDragStart = [this] { draggingPosition = true; };
    positionSlider.onDragEnd   = [this]
    {
        if (player.hasFile())
            player.setPositionSeconds (positionSlider.getValue() * player.getLengthSeconds());
        draggingPosition = false;
    };
    addAndMakeVisible (positionSlider);

    // Franja de estructura del tema (intro/subida/drop/...). Clic = saltar.
    sectionBar.onSeek = [this] (double frac)
    {
        if (player.hasFile())
            player.setPositionSeconds (frac * player.getLengthSeconds());
    };
    sectionBar.setEditable (true);
    sectionBar.onSectionsEdited = [this] (const std::vector<TrackSection>& secs)
    {
        if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
        {
            playlist.setSectionsForTrack (currentlyPlaying, secs);
            updateActiveShow();
        }
    };
    addAndMakeVisible (sectionBar);

    timeLabel.setFont (juce::FontOptions (12.0f));
    timeLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeLabel);

    nowPlayingLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nowPlayingLabel.setColour (juce::Label::textColourId, juce::Colour (P::accent));
    addAndMakeVisible (nowPlayingLabel);

    addAndMakeVisible (preview);
    addChildComponent (stage);

    stage.getAssignedStem = [this] (const juce::String& key) { return playlist.getStemFor (key); };
    stage.onAssignStem = [this] (const juce::String& key, const juce::String& stem)
    {
        playlist.setStemFor (key, stem);   // regenera los shows con la nueva asignacion
        updateActiveShow();
        stage.repaint();
    };

    // Asignacion de stems SOLO para el tema que suena (sobrescribe la global de ese tema).
    stage.songAssignAvailable = [this] { return currentlyPlaying >= 0 && currentlyPlaying < playlist.size(); };
    stage.activeSongName = [this] () -> juce::String
    {
        if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
            return playlist.getTrack (currentlyPlaying).displayName;
        return {};
    };
    stage.getSongStemFor = [this] (const juce::String& key)
    {
        return playlist.getSongStemFor (currentlyPlaying, key);
    };
    stage.onAssignStemForSong = [this] (const juce::String& key, const juce::String& stem)
    {
        playlist.setSongStemFor (currentlyPlaying, key, stem);
        updateActiveShow();
        stage.repaint();
    };

    viewButton.setWantsKeyboardFocus (false);
    viewButton.setTooltip ("Cambia entre la vista de escenario (luces simuladas) y los niveles DMX por canal.");
    viewButton.onClick = [this] { showStage = ! showStage; updateViewMode(); };
    addAndMakeVisible (viewButton);

    perspectiveButton.setWantsKeyboardFocus (false);
    perspectiveButton.setClickingTogglesState (true);
    perspectiveButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (P::control));
    perspectiveButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (P::accent));
    perspectiveButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (P::textMid));
    perspectiveButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (P::bg0));
    perspectiveButton.setTooltip ("Vista en perspectiva (2.5D) con haces de luz. Vuelve a pulsar para la vista plana.");
    perspectiveButton.onClick = [this]
    {
        const bool on = perspectiveButton.getToggleState();
        perspectiveButton.setButtonText (on ? "2.5D On" : "2.5D");
        stage.setPerspective (on);
    };
    addAndMakeVisible (perspectiveButton);

    artNetButton.setWantsKeyboardFocus (false);
    artNetButton.onClick = [this]
    {
        engine.artNet().setEnabled (artNetButton.getToggleState());
        playlist.setArtNetEnabled (artNetButton.getToggleState());
        if (! artNetButton.getToggleState())
            engine.blackout();
    };
    addAndMakeVisible (artNetButton);

    artNetIpLabel.setText ("IP:", juce::dontSendNotification);
    artNetIpLabel.setFont (juce::FontOptions (12.0f));
    artNetIpLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
    artNetIpLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (artNetIpLabel);

    {
        auto savedArtIp = playlist.getArtNetIp();
        if (savedArtIp.isEmpty()) savedArtIp = "127.0.0.1";
        artNetIp.setText (savedArtIp, juce::dontSendNotification);
        engine.artNet().setBroadcast (savedArtIp.isEmpty());
        engine.artNet().setTargetIp (savedArtIp);
    }
    artNetIp.setTooltip ("IP del nodo/visualizador Art-Net (vacio = broadcast)");
    artNetIp.onTextChange = [this]
    {
        const auto ip = artNetIp.getText().trim();
        engine.artNet().setBroadcast (ip.isEmpty());
        if (ip.isNotEmpty())
            engine.artNet().setTargetIp (ip);
        playlist.setArtNetIp (ip);
    };
    addAndMakeVisible (artNetIp);
    artNetButton.setToggleState (playlist.isArtNetEnabled(), juce::dontSendNotification);
    engine.artNet().setEnabled (playlist.isArtNetEnabled());

    sacnButton.setWantsKeyboardFocus (false);
    sacnButton.setTooltip ("Envia sACN (E1.31). Vacio = multicast 239.255.x.x; o pon una IP para unicast.");
    sacnButton.onClick = [this]
    {
        engine.sacn().setEnabled (sacnButton.getToggleState());
        playlist.setSacnEnabled (sacnButton.getToggleState());
        if (! sacnButton.getToggleState())
            engine.blackout();
    };
    addAndMakeVisible (sacnButton);

    sacnIpLabel.setText ("IP:", juce::dontSendNotification);
    sacnIpLabel.setFont (juce::FontOptions (12.0f));
    sacnIpLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
    sacnIpLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (sacnIpLabel);

    {
        const auto savedSacnIp = playlist.getSacnIp();
        sacnIp.setText (savedSacnIp, juce::dontSendNotification);
        engine.sacn().setMulticast (savedSacnIp.isEmpty());
        if (savedSacnIp.isNotEmpty())
            engine.sacn().setTargetIp (savedSacnIp);
    }
    sacnIp.setTooltip ("IP del nodo sACN para unicast (vacio = multicast 239.255.x.x)");
    sacnIp.onTextChange = [this]
    {
        const auto ip = sacnIp.getText().trim();
        engine.sacn().setMulticast (ip.isEmpty());
        if (ip.isNotEmpty())
            engine.sacn().setTargetIp (ip);
        playlist.setSacnIp (ip);
    };
    addAndMakeVisible (sacnIp);
    sacnButton.setToggleState (playlist.isSacnEnabled(), juce::dontSendNotification);
    engine.sacn().setEnabled (playlist.isSacnEnabled());

    netIfaceLabel.setText ("Red:", juce::dontSendNotification);
    netIfaceLabel.setFont (juce::FontOptions (12.0f));
    netIfaceLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
    netIfaceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (netIfaceLabel);

    netIfaceCombo.setTooltip ("Interfaz de red (tarjeta) por la que salen Art-Net y sACN. "
                              "Util si tienes varias redes (WiFi + Ethernet de luces).");
    netIfaceCombo.onChange = [this]
    {
        const int idx = netIfaceCombo.getSelectedItemIndex();
        const juce::String ip = (idx >= 0 && idx < netIfaceIps.size()) ? netIfaceIps[idx] : juce::String();
        engine.artNet().setLocalInterface (ip);
        engine.sacn().setLocalInterface (ip);
        playlist.setNetInterface (ip);
    };
    addAndMakeVisible (netIfaceCombo);
    rescanNetInterfaces();

    enttecButton.setWantsKeyboardFocus (false);
    enttecButton.onClick = [this]
    {
        engine.enttec().setEnabled (enttecButton.getToggleState());
        playlist.setEnttecEnabled (enttecButton.getToggleState());
        if (! enttecButton.getToggleState())
            engine.blackout();
        refreshEnttecUi();
    };
    addAndMakeVisible (enttecButton);

    enttecPortCombo.setTextWhenNothingSelected ("(puerto)");
    enttecPortCombo.onChange = [this]
    {
        const auto p = extractComPortName (enttecPortCombo.getText());
        if (p.isNotEmpty())
        {
            engine.enttec().setPort (p);
            playlist.setEnttecPort (p);
        }
        refreshEnttecUi();
    };
    addAndMakeVisible (enttecPortCombo);

    enttecRefreshButton.setWantsKeyboardFocus (false);
    enttecRefreshButton.onClick = [this] { rescanEnttecPorts(); };
    addAndMakeVisible (enttecRefreshButton);

    enttecUniLabel.setText ("U:", juce::dontSendNotification);
    enttecUniLabel.setFont (juce::FontOptions (12.0f));
    enttecUniLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
    enttecUniLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (enttecUniLabel);

    enttecUniSlider.setSliderStyle (juce::Slider::IncDecButtons);
    enttecUniSlider.setRange (0.0, 7.0, 1.0);
    enttecUniSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
    enttecUniSlider.onValueChange = [this]
    {
        const int u = (int) enttecUniSlider.getValue();
        engine.enttec().setUniverse (u);
        playlist.setEnttecUniverse (u);
    };
    addAndMakeVisible (enttecUniSlider);

    enttecProtocolCombo.setWantsKeyboardFocus (false);
    enttecProtocolCombo.addItem ("USB Pro", 1);
    enttecProtocolCombo.addItem ("Open DMX", 2);
    enttecProtocolCombo.setTooltip ("Protocolo del adaptador. 'USB Pro' = Enttec DMX USB Pro (y compatibles con micro). "
                                    "'Open DMX' = Enttec Open DMX USB y cables FTDI genericos USB-XLR (el PC genera el DMX).");
    enttecProtocolCombo.onChange = [this]
    {
        const int p = enttecProtocolCombo.getSelectedId() - 1;   // 0=USB Pro, 1=Open DMX
        engine.enttec().setProtocol (p == 1 ? EnttecSender::Protocol::OpenDmx
                                            : EnttecSender::Protocol::UsbPro);
        playlist.setEnttecProtocol (p);
    };
    addAndMakeVisible (enttecProtocolCombo);

    enttecStatusLabel.setFont (juce::FontOptions (11.0f));
    enttecStatusLabel.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
    addAndMakeVisible (enttecStatusLabel);
    rescanEnttecPorts();
    refreshEnttecUi();

    // Estado del backend de stems (IA) para la etiqueta informativa de Salida DMX.
    const bool stemsOk = playlist.isStemSeparationAvailable();

    // Pestanas principales.
    auto setupTab = [this] (juce::TextButton& b, int tab)
    {
        b.setWantsKeyboardFocus (false);
        b.setClickingTogglesState (false);
        b.onClick = [this, tab] { setTab (tab); };
        addAndMakeVisible (b);
    };
    setupTab (tabPlayerButton, 0);
    setupTab (tabStageButton, 1);
    setupTab (tabStemsButton, 2);
    setupTab (tabCustomButton, 3);
    setupTab (tabCreatorButton, 4);
    setupTab (tabPianoButton, 6);
    setupTab (tabOutputButton, 5);

    // Modo del Creador (manual vs parametrico).
    creatorManualButton.setWantsKeyboardFocus (false);
    creatorManualButton.setClickingTogglesState (false);
    creatorManualButton.onClick = [this] { setCreatorMode (false); };
    addAndMakeVisible (creatorManualButton);

    creatorParamButton.setWantsKeyboardFocus (false);
    creatorParamButton.setClickingTogglesState (false);
    creatorParamButton.onClick = [this] { setCreatorMode (true); };
    addAndMakeVisible (creatorParamButton);

    audioButton.setWantsKeyboardFocus (false);
    audioButton.setTooltip ("Elige la salida de audio (altavoces / interfaz) y la frecuencia de muestreo.");
    audioButton.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible (audioButton);

    testButton.setWantsKeyboardFocus (false);
    testButton.setClickingTogglesState (true);
    testButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (P::accent));
    testButton.setTooltip ("Enciende TODAS las luces del rig a tope (sin musica) para comprobar direccion DMX y cableado.");
    testButton.onClick = [this] { toggleTest(); };
    addAndMakeVisible (testButton);

    // Selector de ESTILO del show (dinamica de patron/color de las luces).
    styleLabel.setText ("Estilo:", juce::dontSendNotification);
    styleLabel.setFont (juce::FontOptions (12.0f));
    styleLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
    styleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (styleLabel);

    styleCombo.setWantsKeyboardFocus (false);
    styleCombo.setTooltip ("Cambia como encienden/apagan las luces y la dinamica de color: chase, alterno, onda, arcoiris, pulso...");
    {
        const auto names = playlist.getStyleNames();
        for (int i = 0; i < names.size(); ++i)
            styleCombo.addItem (names[i], i + 1);
    }
    styleCombo.onChange = [this]
    {
        playlist.setStyleIndex (styleCombo.getSelectedId() - 1);
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    addAndMakeVisible (styleCombo);

    // Knobs de offset de brillo (barra de pixeles): color y blanco por separado.
    auto setupOffsetKnob = [this] (juce::Slider& k, juce::Label& lab, const juce::String& text,
                                   const juce::String& tip)
    {
        lab.setText (text, juce::dontSendNotification);
        lab.setFont (juce::FontOptions (11.0f));
        lab.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        lab.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (lab);

        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setRange (0.0, 1.0, 0.01);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setWantsKeyboardFocus (false);
        k.setTooltip (tip);
        addAndMakeVisible (k);
    };
    setupOffsetKnob (colorOffsetKnob, colorOffsetLabel, "Color",
                     "Brillo de base anadido a los canales de COLOR (RGB) de la barra de pixeles. 0 = solo musica.");
    setupOffsetKnob (whiteOffsetKnob, whiteOffsetLabel, "Blanco",
                     "Brillo de base anadido a los canales de BLANCO de la barra de pixeles. 0 = solo acentos.");
    colorOffsetKnob.onValueChange = [this]
    {
        // Arrastre: regenera SOLO el tema que suena (barato) para feedback en vivo;
        // ademas refresca el preview en reposo. Sin tocar el resto ni el disco.
        playlist.setColorOffsetLive ((float) colorOffsetKnob.getValue());
        playlist.regenerateShow (currentlyPlaying);
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    colorOffsetKnob.onDragEnd = [this]
    {
        // Al soltar: regenera todas las coreografias y guarda (trabajo pesado, 1 vez).
        playlist.commitOffsets();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    whiteOffsetKnob.onValueChange = [this]
    {
        playlist.setWhiteOffsetLive ((float) whiteOffsetKnob.getValue());
        playlist.regenerateShow (currentlyPlaying);
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    whiteOffsetKnob.onDragEnd = [this]
    {
        playlist.commitOffsets();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };

    // Re-entrenar: re-separa los stems del tema seleccionado (con confirmacion).
    retrainButton.setWantsKeyboardFocus (false);
    retrainButton.setTooltip ("Genera (separa) los stems con IA del tema seleccionado, ignorando la cache. Tarda varios minutos por cancion.");
    retrainButton.onClick = [this] { retrainSelected(); };
    addAndMakeVisible (retrainButton);

    // Colores preferidos: el tema usara solo esos colores en su coreografia.
    colorsButton.setWantsKeyboardFocus (false);
    colorsButton.setTooltip ("Elige los colores preferidos del tema seleccionado. La coreografia usara solo esos.");
    colorsButton.onClick = [this] { editPreferredColors(); };
    addAndMakeVisible (colorsButton);

    // Coreografias del tema: elige de la libreria cuales usar (control antiguo, oculto;
    // ahora se accede desde el boton "Coreografias..." del Reproductor).
    songChoreoButton.setWantsKeyboardFocus (false);
    songChoreoButton.setTooltip ("Elige las coreografias para el tema seleccionado. El Auto IA las reparte por energia.");
    songChoreoButton.onClick = [this]
    {
        if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
            editSongChoreosForFile (playlist.getTrack (currentlyPlaying).file, {}, {});
    };
    addAndMakeVisible (songChoreoButton);

    stemStatusLabel.setFont (juce::FontOptions (11.0f));
    stemStatusLabel.setColour (juce::Label::textColourId, juce::Colour (stemsOk ? P::textMid : P::textDim));
    stemStatusLabel.setText (stemsOk ? playlist.getStemBackendName()
                                     : "Stems IA no disponible (instala Python + demucs)",
                             juce::dontSendNotification);
    addAndMakeVisible (stemStatusLabel);

    playlist.addChangeListener (this);

    // Construye el "show en reposo" = el rig por defecto con las luces apagadas,
    // para que el escenario se vea desde el arranque aunque no haya tema activo.
    rebuildIdleShow();

    // Recupera la sesion previa (lista de temas + opciones).
    playlist.loadSession();
    playlist.seedDefaultsIfNeeded();   // siembra la barra LED de ejemplo (una vez)
    rebuildIdleShow();
    styleCombo.setSelectedId (playlist.getStyleIndex() + 1, juce::dontSendNotification);
    stage.setMoveFigure (playlist.getMoveFigureIndex());
    colorOffsetKnob.setValue (playlist.getColorOffset(), juce::dontSendNotification);
    whiteOffsetKnob.setValue (playlist.getWhiteOffset(), juce::dontSendNotification);
    list.updateContent();
    updateRetrainEnabled();

    // Restaura configuracion Enttec desde la sesion guardada.
    rescanEnttecPorts();
    syncDmxOutputsFromSession();

    // Paneles de las pestanas Stems / Equipos Custom (cubren el area de contenido).
    stemPanel = std::make_unique<StemAssignPanel> (playlist);
    stemPanel->onAssignmentsChanged = [this]
    {
        rebuildIdleShow();
        updateActiveShow();
        stage.repaint();
        preview.repaint();
    };
    addChildComponent (stemPanel.get());

    customPanel = std::make_unique<CustomFixturePanel> (playlist);
    addChildComponent (customPanel.get());

    choreoPanel = std::make_unique<ChoreoPanel> (playlist);
    addChildComponent (choreoPanel.get());

    seqEditor = std::make_unique<SequenceEditorPanel> (playlist);
    addChildComponent (seqEditor.get());

    // Reproductor unificado (Fase 3): biblioteca + playlists + reproduccion.
    libraryPanel = std::make_unique<LibraryPanel> (musicLibrary, playlist);
    libraryPanel->onPlayQueue = [this] (const juce::Array<juce::File>& files, int startIndex)
    {
        startQueue (files, startIndex);   // misma pestana, no cambia de vista
    };
    libraryPanel->onTrainFiles = [this] (const juce::Array<juce::File>& files)
    {
        for (const auto& f : files)
            playlist.trainFile (f);
    };
    libraryPanel->onShuffleChanged = [this] (bool on)
    {
        shuffleMode = on;
        // Si esta sonando algo, rehace el orden manteniendo el tema actual como inicio.
        if (currentlyPlaying >= 0 && ! playQueue.isEmpty())
        {
            const auto cur = playlist.getTrack (currentlyPlaying).file;
            int curIdx = playQueue.indexOf (cur);
            rebuildPlayOrder (curIdx);
        }
    };
    libraryPanel->onRepeatChanged = [this] (bool on) { repeatMode = on; };
    libraryPanel->onRegenerate = [this] () -> int
    {
        const int n = playlist.regenerateShows();
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
        return n;
    };
    libraryPanel->onEditColors  = [this] (const juce::File& f) { editPreferredColorsForFile (f); };
    libraryPanel->onEditChoreos = [this] (const juce::File& f, const juce::StringArray& paths, const juce::String& name)
    {
        editSongChoreosForFile (f, paths, name);
    };
    libraryPanel->onEditManual = [this] (int pi) { openManualEditor (pi); };
    libraryPanel->onShowProperties = [this] (const juce::File& f) { showSongProperties (f); };
    libraryPanel->onStyleChanged = [this]
    {
        stage.setMoveFigure (playlist.getMoveFigureIndex());
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    libraryPanel->getNowPlayingFile = [this] () -> juce::File
    {
        if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
            return playlist.getTrack (currentlyPlaying).file;
        return {};
    };
    addChildComponent (libraryPanel.get());

    // Pestana Piano roll: editor de coreografias manuales integrado.
    pianoPanel = std::make_unique<PianoRollPanel> (playlist);
    pianoPanel->onShowEdited = [this]
    {
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    pianoPanel->getPlayheadSecondsFor = [this] (int idx) -> double
    {
        return (currentlyPlaying == idx && player.hasFile())
                 ? player.getPositionSeconds() : -1.0;
    };
    pianoPanel->isPlayingFor = [this] (int idx) -> bool
    {
        return currentlyPlaying == idx && player.isPlaying();
    };
    pianoPanel->onTogglePlay = [this] (int idx)
    {
        if (currentlyPlaying == idx && player.hasFile())
            togglePlayPause();
        else
            playTrack (idx);
    };
    pianoPanel->onSeekSeconds = [this] (int idx, double secs)
    {
        if (currentlyPlaying != idx || ! player.hasFile())
            playTrack (idx);
        if (currentlyPlaying == idx && player.hasFile())
            player.setPositionSeconds (juce::jlimit (0.0, player.getLengthSeconds(), secs));
    };
    pianoPanel->onOpenStageWindow = [this] { openStageWindow(); };
    addChildComponent (pianoPanel.get());

    setTab (0);

    updateViewMode();
    updateActiveShow();
    setSize (1040, 720);
    startTimerHz (30);

    // Autocarga del ultimo proyecto .lux (si esta activado en preferencias).
    if (playlist.getAutoLoadLastProject())
    {
        const auto recents = playlist.getRecentProjects();
        if (recents.size() > 0 && recents[0].existsAsFile())
            playlist.openProjectFile (recents[0]);
    }
}

AutomatorComponent::~AutomatorComponent()
{
    stopTimer();
    player.saveDeviceState();
    engine.enttec().setEnabled (false);
    engine.artNet().setEnabled (false);
    playlist.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void AutomatorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (P::bg1));

    // Punto luminoso junto al titulo (la cabecera va debajo de la barra de menus).
    g.setColour (juce::Colour (P::accent));
    g.fillEllipse (16.0f, 44.0f, 8.0f, 8.0f);

    // Separador bajo la barra de menus.
    g.setColour (juce::Colour (P::line));
    g.drawHorizontalLine (26, 0.0f, (float) getWidth());

    // Separador bajo la barra de pestanas.
    g.drawHorizontalLine (100, 0.0f, (float) getWidth());
}

void AutomatorComponent::paintOverChildren (juce::Graphics& g)
{
    // Aviso de apagon de emergencia activo: marco rojo + banner, encima de todo.
    if (! engine.isBlackout())
        return;

    auto bounds = getLocalBounds().toFloat();

    // Velo rojo muy tenue para que se note el estado sin tapar la UI.
    g.setColour (juce::Colour (0xffd11a2a).withAlpha (0.06f));
    g.fillRect (bounds);

    // Marco rojo pulsante.
    const float pulse = 0.55f + 0.45f * (float) std::sin (juce::Time::getMillisecondCounterHiRes() * 0.006);
    g.setColour (juce::Colour (0xffd11a2a).withAlpha (0.35f + 0.4f * pulse));
    g.drawRect (bounds, 3.0f);

    // Banner "BLACKOUT" centrado arriba.
    const float bw = 220.0f, bh = 30.0f;
    juce::Rectangle<float> banner ((getWidth() - bw) * 0.5f, 30.0f, bw, bh);
    g.setColour (juce::Colour (0xff2a0608).withAlpha (0.9f));
    g.fillRoundedRectangle (banner, 6.0f);
    g.setColour (juce::Colour (0xffd11a2a).withAlpha (0.6f + 0.4f * pulse));
    g.drawRoundedRectangle (banner, 6.0f, 1.5f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("BLACKOUT  -  B / Esc", banner, juce::Justification::centred);
}

void AutomatorComponent::resized()
{
    auto area = getLocalBounds();

    // Barra de menus tipo DAW.
    menuBar.setBounds (area.removeFromTop (26));

    // Cabecera con titulo.
    auto header = area.removeFromTop (40);
    titleLabel.setBounds (header.withTrimmedLeft (32).withTrimmedTop (4));

    // Barra de pestanas.
    auto tabBar = area.removeFromTop (34);
    tabBar.reduce (12, 4);
    tabPlayerButton.setBounds (tabBar.removeFromLeft (110)); tabBar.removeFromLeft (6);
    tabStageButton .setBounds (tabBar.removeFromLeft (96));  tabBar.removeFromLeft (6);
    tabStemsButton .setBounds (tabBar.removeFromLeft (80));  tabBar.removeFromLeft (6);
    tabCustomButton.setBounds (tabBar.removeFromLeft (130)); tabBar.removeFromLeft (6);
    tabCreatorButton.setBounds (tabBar.removeFromLeft (84)); tabBar.removeFromLeft (6);
    tabPianoButton .setBounds (tabBar.removeFromLeft (96));  tabBar.removeFromLeft (6);
    tabOutputButton.setBounds (tabBar.removeFromLeft (104));

    // Transporte persistente abajo (visible en TODAS las pestanas).
    auto transport = area.removeFromBottom (84);
    {
        auto t = transport.reduced (12, 0);
        auto top = t.removeFromTop (30);
        playButton.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (6);
        stopButton.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (12);
        blackoutButton.setBounds (top.removeFromRight (96));
        top.removeFromRight (10);
        timeLabel.setBounds (top.removeFromRight (110));
        nowPlayingLabel.setBounds (top);

        t.removeFromTop (6);
        sectionBar.setBounds (t.removeFromTop (16));
        t.removeFromTop (4);
        positionSlider.setBounds (t.removeFromTop (20));
    }

    // Paneles que cubren toda el area de contenido (Stems / Equipos Custom).
    if (stemPanel != nullptr)   stemPanel->setBounds (area);
    if (customPanel != nullptr) customPanel->setBounds (area);

    // Creador: franja de modo (Manual / Parametrico) arriba + panel debajo.
    {
        auto creatorArea = area;
        auto modeStrip = creatorArea.removeFromTop (40).reduced (12, 6);
        creatorManualButton.setBounds (modeStrip.removeFromLeft (110));
        modeStrip.removeFromLeft (8);
        creatorParamButton.setBounds (modeStrip.removeFromLeft (130));
        if (choreoPanel != nullptr) choreoPanel->setBounds (creatorArea);
        if (seqEditor != nullptr)   seqEditor->setBounds (creatorArea);
    }

    if (libraryPanel != nullptr) libraryPanel->setBounds (area);
    if (pianoPanel != nullptr)   pianoPanel->setBounds (area);

    auto content = area.reduced (12, 12);

    if (currentTab == 0)
    {
        // --- Reproductor: estilo arriba, lista grande, gestion a la derecha ---
        auto controls = content.removeFromTop (28);
        styleLabel.setBounds (controls.removeFromLeft (48));
        styleCombo.setBounds (controls.removeFromLeft (170));
        retrainButton.setBounds (controls.removeFromRight (124));
        controls.removeFromRight (8);
        colorsButton.setBounds (controls.removeFromRight (100));
        controls.removeFromRight (8);
        songChoreoButton.setBounds (controls.removeFromRight (124));
        content.removeFromTop (10);

        auto sidebar = content.removeFromRight (110);
        auto sideBtn = [&sidebar] (juce::Component& c)
        {
            c.setBounds (sidebar.removeFromTop (32));
            sidebar.removeFromTop (8);
        };
        sideBtn (addButton);
        sideBtn (removeButton);
        sidebar.removeFromTop (8);
        sideBtn (upButton);
        sideBtn (downButton);
        content.removeFromRight (12);

        list.setBounds (content);
    }
    else if (currentTab == 1)
    {
        // --- Escenario: visualizador a pantalla completa + controles arriba ---
        auto controls = content.removeFromTop (40);
        viewButton.setBounds (controls.removeFromLeft (110).withSizeKeepingCentre (110, 28));
        controls.removeFromLeft (8);
        perspectiveButton.setBounds (controls.removeFromLeft (70).withSizeKeepingCentre (70, 28));
        whiteOffsetKnob .setBounds (controls.removeFromRight (38));
        whiteOffsetLabel.setBounds (controls.removeFromRight (48));
        controls.removeFromRight (10);
        colorOffsetKnob .setBounds (controls.removeFromRight (38));
        colorOffsetLabel.setBounds (controls.removeFromRight (44));
        content.removeFromTop (8);

        preview.setBounds (content);
        stage.setBounds (content);
    }
    else if (currentTab == 5)
    {
        // --- Salida DMX: configuracion de red y adaptador ---
        auto row1 = content.removeFromTop (30);
        artNetButton.setBounds (row1.removeFromLeft (90));
        row1.removeFromLeft (8);
        artNetIpLabel.setBounds (row1.removeFromLeft (28));
        artNetIp.setBounds (row1.removeFromLeft (130));
        content.removeFromTop (10);

        auto rowS = content.removeFromTop (30);
        sacnButton.setBounds (rowS.removeFromLeft (90));
        rowS.removeFromLeft (8);
        sacnIpLabel.setBounds (rowS.removeFromLeft (28));
        sacnIp.setBounds (rowS.removeFromLeft (130));
        rowS.removeFromLeft (16);
        netIfaceLabel.setBounds (rowS.removeFromLeft (34));
        netIfaceCombo.setBounds (rowS.removeFromLeft (190));
        content.removeFromTop (10);

        auto row2 = content.removeFromTop (30);
        enttecButton.setBounds (row2.removeFromLeft (82));
        row2.removeFromLeft (8);
        enttecPortCombo.setBounds (row2.removeFromLeft (130));
        row2.removeFromLeft (8);
        enttecRefreshButton.setBounds (row2.removeFromLeft (70));
        row2.removeFromLeft (12);
        enttecUniLabel.setBounds (row2.removeFromLeft (20));
        enttecUniSlider.setBounds (row2.removeFromLeft (72));
        row2.removeFromLeft (12);
        enttecProtocolCombo.setBounds (row2.removeFromLeft (100));
        content.removeFromTop (8);

        auto row3 = content.removeFromTop (24);
        enttecStatusLabel.setBounds (row3);
        content.removeFromTop (14);

        auto row4 = content.removeFromTop (30);
        testButton.setBounds (row4.removeFromLeft (90));
        row4.removeFromLeft (8);
        audioButton.setBounds (row4.removeFromLeft (90));
        content.removeFromTop (8);

        stemStatusLabel.setBounds (content.removeFromTop (22));
    }
}

//==============================================================================
int AutomatorComponent::getNumRows()
{
    return playlist.size();
}

void AutomatorComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= playlist.size())
        return;

    const auto& t = playlist.getTrack (row);
    const bool isPlaying = (row == currentlyPlaying);

    if (selected)
        g.fillAll (juce::Colour (P::accent).withAlpha (0.18f));
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.02f));

    // Indicador de reproduccion
    if (isPlaying)
    {
        g.setColour (juce::Colour (0xff62d96b));
        juce::Path tri;
        tri.addTriangle (10.0f, height * 0.5f - 5.0f, 10.0f, height * 0.5f + 5.0f, 18.0f, height * 0.5f);
        g.fillPath (tri);
    }

    auto r = juce::Rectangle<int> (26, 0, width - 26, height);

    // Estado / color de texto
    juce::Colour nameColour = juce::Colour (P::textHi);
    if (t.state == Track::State::Error)
        nameColour = juce::Colour (0xffe05050);

    g.setColour (nameColour);
    g.setFont (juce::FontOptions (13.0f, isPlaying ? juce::Font::bold : juce::Font::plain));
    g.drawText (juce::String (row + 1) + ".  " + t.displayName,
                r.removeFromLeft (width - 250), juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (P::textDim));
    g.setFont (juce::FontOptions (11.0f));
    juce::String stateText = Track::stateLabel (t.state);
    if (t.analysis.valid && t.analysis.estimatedBpm > 0.0)
        stateText += "  " + juce::String (juce::roundToInt (t.analysis.estimatedBpm)) + " BPM";
    g.drawText (stateText, r.removeFromLeft (130),
                juce::Justification::centredLeft, false);

    // Indicador de show DMX generado (azul "stems" si hay separacion por instrumento).
    if (t.show.valid)
    {
        const bool hasStems = ! t.analysis.stems.empty();
        g.setColour (juce::Colour (hasStems ? P::accent2 : P::accent));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (hasStems ? "stems" : "show", r.removeFromLeft (44), juce::Justification::centredLeft, false);
    }

    g.setColour (juce::Colour (P::textMid));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (t.lengthString(), r, juce::Justification::centredRight, false);

    // Barra de progreso fina al pie de la fila.
    const int barH = 3;
    juce::Rectangle<float> bar ((float) 26, (float) (height - barH), (float) (width - 26 - 8), (float) barH);

    if (t.state == Track::State::Analyzing || t.state == Track::State::Decoding)
    {
        // Indeterminada: un segmento luminoso que recorre la barra.
        g.setColour (juce::Colour (P::control));
        g.fillRoundedRectangle (bar, 1.5f);

        const float segFrac = 0.32f;
        const float segW = bar.getWidth() * segFrac;
        const float travel = bar.getWidth() + segW;
        float pos = (float) std::fmod (animPhase, 1.0) * travel - segW;

        juce::Rectangle<float> seg (bar.getX() + pos, bar.getY(), segW, bar.getHeight());
        seg = seg.getIntersection (bar);
        if (! seg.isEmpty())
        {
            g.setColour (juce::Colour (P::accent2));
            g.fillRoundedRectangle (seg, 1.5f);
        }
    }
    else if (t.state == Track::State::Ready && t.show.valid)
    {
        // Completado: barra llena tenue en ambar.
        g.setColour (juce::Colour (P::accent).withAlpha (0.55f));
        g.fillRoundedRectangle (bar, 1.5f);
    }
    else if (t.state == Track::State::Pending)
    {
        // En cola: barra de fondo apenas visible.
        g.setColour (juce::Colour (P::control).withAlpha (0.6f));
        g.fillRoundedRectangle (bar, 1.5f);
    }
}

void AutomatorComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    playTrack (row);
}

void AutomatorComponent::deleteKeyPressed (int lastRowSelected)
{
    if (lastRowSelected >= 0)
        playlist.removeTrack (lastRowSelected);
}

//==============================================================================
void AutomatorComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    list.updateContent();
    list.repaint();
    // El rig o el vector de temas pudo cambiar: re-resolver el show activo.
    rebuildIdleShow();
    updateActiveShow();
    updateRetrainEnabled();
    if (stemPanel != nullptr)   stemPanel->refreshAll();
    if (customPanel != nullptr) customPanel->refreshFromPlaylist();
    if (pianoPanel != nullptr)  pianoPanel->refreshSongList();
    syncDmxOutputsFromSession();
}

void AutomatorComponent::timerCallback()
{
    // Encadena el siguiente tema al terminar.
    if (player.hasReachedEnd())
        playNext();

    // Refresca el transporte.
    const double len = player.getLengthSeconds();
    const double pos = player.getPositionSeconds();

    if (! draggingPosition)
        positionSlider.setValue (len > 0.0 ? pos / len : 0.0, juce::dontSendNotification);

    sectionBar.setPosition (pos);

    timeLabel.setText (formatTime (pos) + " / " + formatTime (len), juce::dontSendNotification);

    // Mantiene la animacion del marco de blackout activo.
    if (engine.isBlackout())
        repaint();

    playButton.setButtonText (player.isPlaying() ? "Pause" : "Play");

    if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
        nowPlayingLabel.setText (playlist.getTrack (currentlyPlaying).displayName, juce::dontSendNotification);
    else
        nowPlayingLabel.setText ("(sin reproducir)", juce::dontSendNotification);

    // Muestrea el show en la posicion actual, emite por Art-Net y previsualiza.
    const bool creatorTesting = (seqEditor != nullptr && currentTab == 4 && seqEditor->isTesting());
    if (creatorTesting)
        engine.sendTestFrame (seqEditor->buildCurrentFrame());   // prueba del Creador manda
    else if (testActive)
        engine.sendTestFrame (buildTestFrame());   // prueba fija: ignora la musica
    else
        engine.tick (pos);

    // Al dejar de probar en el Creador, apaga el rig una vez.
    if (creatorWasTesting && ! creatorTesting)
        engine.blackout();
    creatorWasTesting = creatorTesting;

    stage.setPlayheadSeconds (pos);
    if (showStage) stage.refreshFrom (engine);
    else           preview.refreshFrom (engine);
    if (popoutStage != nullptr)
    {
        popoutStage->setPlayheadSeconds (pos);
        popoutStage->refreshFrom (engine);
    }
    if (engine.enttec().isEnabled())
        refreshEnttecUi();

    // Anima la barra de progreso de las filas en analisis.
    bool anyAnalyzing = false;
    for (int i = 0; i < playlist.size(); ++i)
    {
        const auto st = playlist.getTrack (i).state;
        if (st == Track::State::Analyzing || st == Track::State::Decoding)
        {
            anyAnalyzing = true;
            break;
        }
    }
    if (anyAnalyzing)
    {
        animPhase += 1.0 / 30.0;   // ~1 recorrido por segundo (timer a 30 Hz)
        list.repaint();
    }
}

void AutomatorComponent::rescanEnttecPorts()
{
    const auto ports = EnttecSender::getAvailablePorts();
    const auto current = extractComPortName (engine.enttec().getPort());

    enttecPortCombo.clear (juce::dontSendNotification);
    int idToSelect = 0;
    int autoId = 0;
    int bestScore = -1;
    for (int i = 0; i < ports.size(); ++i)
    {
        enttecPortCombo.addItem (ports[i], i + 1);
        if (extractComPortName (ports[i]) == current)
            idToSelect = i + 1;

        const int score = preferredPortScore (ports[i]);
        if (score > bestScore)
        {
            bestScore = score;
            autoId = i + 1;
        }
    }

    if (idToSelect > 0)
        enttecPortCombo.setSelectedId (idToSelect, juce::dontSendNotification);
    else if (current.isEmpty() && autoId > 0 && bestScore > 0)
    {
        enttecPortCombo.setSelectedId (autoId, juce::dontSendNotification);
        const auto autoPort = extractComPortName (enttecPortCombo.getText());
        engine.enttec().setPort (autoPort);
        playlist.setEnttecPort (autoPort);
    }
    else if (current.isNotEmpty())
    {
        enttecPortCombo.addItem (current + " (ausente)", ports.size() + 1);
        enttecPortCombo.setSelectedId (ports.size() + 1, juce::dontSendNotification);
    }
}

void AutomatorComponent::refreshEnttecUi()
{
    const bool on = engine.enttec().isEnabled();
    enttecPortCombo.setEnabled (on);
    enttecRefreshButton.setEnabled (on);
    enttecUniLabel.setEnabled (on);
    enttecUniSlider.setEnabled (on);
    enttecProtocolCombo.setEnabled (on);

    juce::String s;
    juce::Colour c = juce::Colour (P::textDim);
    if (! on)
    {
        s = "Enttec: desactivado";
    }
    else if (engine.enttec().getPort().isEmpty())
    {
        s = "Enttec: elige un puerto";
        c = juce::Colour (P::textMid);
    }
    else if (engine.enttec().isConnected())
    {
        s = "Enttec: conectado " + engine.enttec().getPort()
          + " (U" + juce::String (engine.enttec().getUniverse()) + ")";
        c = juce::Colour (0xff3bd45f);
    }
    else
    {
        s = "Enttec: sin conexion (revisa cable/puerto)";
        c = juce::Colour (0xffffb020);
    }

    enttecStatusLabel.setText (s, juce::dontSendNotification);
    enttecStatusLabel.setColour (juce::Label::textColourId, c);
}

void AutomatorComponent::rescanNetInterfaces()
{
    netIfaceCombo.clear (juce::dontSendNotification);
    netIfaceIps.clear();

    // Item 1 = Auto (deja que el sistema elija la ruta).
    netIfaceCombo.addItem ("Auto (sistema)", 1);
    netIfaceIps.add (juce::String());

    int id = 2;
    for (const auto& addr : juce::IPAddress::getAllAddresses (false))   // solo IPv4
    {
        if (addr == juce::IPAddress::any())
            continue;
        netIfaceCombo.addItem (addr.toString(), id++);
        netIfaceIps.add (addr.toString());
    }

    const auto saved = playlist.getNetInterface();
    int sel = 1;   // Auto por defecto
    if (saved.isNotEmpty())
    {
        const int idx = netIfaceIps.indexOf (saved);
        if (idx >= 0)
            sel = idx + 1;
        else
        {
            // Guardada pero ahora ausente: la mostramos para no perderla.
            netIfaceCombo.addItem (saved + " (ausente)", id);
            netIfaceIps.add (saved);
            sel = id;
        }
    }
    netIfaceCombo.setSelectedId (sel, juce::dontSendNotification);

    const juce::String ip = (sel - 1 < netIfaceIps.size()) ? netIfaceIps[sel - 1] : juce::String();
    engine.artNet().setLocalInterface (ip);
    engine.sacn().setLocalInterface (ip);
}

void AutomatorComponent::syncDmxOutputsFromSession()
{
    // --- Art-Net ---
    {
        auto artIp = playlist.getArtNetIp();
        if (artIp.isEmpty()) artIp = "127.0.0.1";
        artNetIp.setText (artIp, juce::dontSendNotification);
        engine.artNet().setBroadcast (artIp.isEmpty());
        engine.artNet().setTargetIp (artIp);
        artNetButton.setToggleState (playlist.isArtNetEnabled(), juce::dontSendNotification);
        engine.artNet().setEnabled (playlist.isArtNetEnabled());
    }

    // --- sACN ---
    {
        const auto sIp = playlist.getSacnIp();
        sacnIp.setText (sIp, juce::dontSendNotification);
        engine.sacn().setMulticast (sIp.isEmpty());
        if (sIp.isNotEmpty())
            engine.sacn().setTargetIp (sIp);
        sacnButton.setToggleState (playlist.isSacnEnabled(), juce::dontSendNotification);
        engine.sacn().setEnabled (playlist.isSacnEnabled());
    }

    // --- Interfaz de red (NIC de salida para Art-Net/sACN) ---
    {
        const auto iface = playlist.getNetInterface();
        int idx = netIfaceIps.indexOf (iface);
        if (idx < 0 && iface.isNotEmpty())
        {
            netIfaceCombo.addItem (iface + " (ausente)", netIfaceCombo.getNumItems() + 1);
            netIfaceIps.add (iface);
            idx = netIfaceIps.size() - 1;
        }
        if (idx < 0) idx = 0;   // Auto
        netIfaceCombo.setSelectedItemIndex (idx, juce::dontSendNotification);
        engine.artNet().setLocalInterface (iface);
        engine.sacn().setLocalInterface (iface);
    }

    // --- Enttec ---
    const auto savedPort = playlist.getEnttecPort();
    if (savedPort.isNotEmpty())
    {
        engine.enttec().setPort (savedPort);
        enttecPortCombo.setText (savedPort, juce::dontSendNotification);
    }
    engine.enttec().setUniverse (playlist.getEnttecUniverse());
    enttecUniSlider.setValue (playlist.getEnttecUniverse(), juce::dontSendNotification);
    {
        const int proto = playlist.getEnttecProtocol();   // 0=USB Pro, 1=Open DMX
        engine.enttec().setProtocol (proto == 1 ? EnttecSender::Protocol::OpenDmx
                                                : EnttecSender::Protocol::UsbPro);
        enttecProtocolCombo.setSelectedId (proto + 1, juce::dontSendNotification);
    }
    const bool enttecOn = playlist.isEnttecEnabled() && savedPort.isNotEmpty();
    engine.enttec().setEnabled (enttecOn);
    enttecButton.setToggleState (enttecOn, juce::dontSendNotification);

    refreshEnttecUi();
}

void AutomatorComponent::setTab (int tab)
{
    currentTab = juce::jlimit (0, 6, tab);

    const bool isPlayerTab = currentTab == 0;
    const bool isStems   = currentTab == 2;
    const bool isCustom  = currentTab == 3;
    const bool isCreator = currentTab == 4;
    const bool isPiano   = currentTab == 6;

    if (stemPanel != nullptr)
    {
        stemPanel->setVisible (isStems);
        if (isStems) { stemPanel->refreshAll(); stemPanel->toFront (false); }
    }
    if (customPanel != nullptr)
    {
        customPanel->setVisible (isCustom);
        if (isCustom) { customPanel->refreshFromPlaylist(); customPanel->toFront (false); }
    }
    // Creador: muestra el panel manual o el parametrico segun el modo.
    const bool showParam  = isCreator && creatorParametric;
    const bool showManual = isCreator && ! creatorParametric;
    if (choreoPanel != nullptr)
    {
        choreoPanel->setVisible (showParam);
        if (showParam) { choreoPanel->refreshFromPlaylist(); choreoPanel->toFront (false); }
    }
    if (seqEditor != nullptr)
    {
        seqEditor->setVisible (showManual);
        if (showManual) { seqEditor->refreshFromPlaylist(); seqEditor->toFront (false); }
    }
    creatorManualButton.setVisible (isCreator);
    creatorParamButton.setVisible (isCreator);
    if (isCreator)
    {
        creatorManualButton.toFront (false);
        creatorParamButton.toFront (false);
    }
    if (libraryPanel != nullptr)
    {
        libraryPanel->setVisible (isPlayerTab);
        if (isPlayerTab) { libraryPanel->refreshFromLibrary(); libraryPanel->toFront (false); }
    }
    if (pianoPanel != nullptr)
    {
        pianoPanel->setVisible (isPiano);
        if (isPiano) { pianoPanel->refreshSongList(); pianoPanel->toFront (false); }
    }

    auto styleTab = [] (juce::TextButton& b, bool active)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (active ? P::accent : P::control));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (active ? P::bg0 : P::textMid));
    };
    styleTab (tabPlayerButton, currentTab == 0);
    styleTab (tabStageButton,  currentTab == 1);
    styleTab (tabStemsButton,  isStems);
    styleTab (tabCustomButton, isCustom);
    styleTab (tabCreatorButton, isCreator);
    styleTab (tabPianoButton,  isPiano);
    styleTab (tabOutputButton, currentTab == 5);
    styleTab (creatorManualButton, showManual);
    styleTab (creatorParamButton,  showParam);

    updateTabVisibility();
    resized();
}

void AutomatorComponent::setCreatorMode (bool parametric)
{
    creatorParametric = parametric;
    if (currentTab == 4)
        setTab (4);
}

void AutomatorComponent::updateTabVisibility()
{
    const bool isStage  = currentTab == 1;
    const bool isOutput = currentTab == 5;

    // --- Pestana Reproductor (tab 0): la cubre libraryPanel; ocultamos los
    //     controles del reproductor antiguo (ya integrados en el panel). ---
    list.setVisible (false);
    addButton.setVisible (false);
    removeButton.setVisible (false);
    upButton.setVisible (false);
    downButton.setVisible (false);
    styleLabel.setVisible (false);
    styleCombo.setVisible (false);
    retrainButton.setVisible (false);
    colorsButton.setVisible (false);
    songChoreoButton.setVisible (false);

    // --- Pestana Escenario: visualizador + offsets + toggle vista ---
    stage.setVisible (isStage && showStage);
    preview.setVisible (isStage && ! showStage);
    viewButton.setVisible (isStage);
    perspectiveButton.setVisible (isStage && showStage);
    colorOffsetLabel.setVisible (isStage);
    colorOffsetKnob.setVisible (isStage);
    whiteOffsetLabel.setVisible (isStage);
    whiteOffsetKnob.setVisible (isStage);

    // --- Pestana Salida DMX: Art-Net + Enttec + prueba + audio + stems ---
    artNetButton.setVisible (isOutput);
    artNetIpLabel.setVisible (isOutput);
    artNetIp.setVisible (isOutput);
    enttecButton.setVisible (isOutput);
    enttecPortCombo.setVisible (isOutput);
    enttecRefreshButton.setVisible (isOutput);
    enttecUniLabel.setVisible (isOutput);
    enttecUniSlider.setVisible (isOutput);
    enttecProtocolCombo.setVisible (isOutput);
    enttecStatusLabel.setVisible (isOutput);
    stemStatusLabel.setVisible (isOutput);
    testButton.setVisible (isOutput);
    audioButton.setVisible (isOutput);
}

void AutomatorComponent::rebuildIdleShow()
{
    idleShow.fixtures = playlist.getRig();
    idleShow.bpm = 120.0;
    idleShow.lengthSeconds = 0.0;

    // Brillo de base de la barra de pixeles (offsets) para que se vea SIN cancion.
    const float colorOff = playlist.getColorOffset();
    const float whiteOff = playlist.getWhiteOffset();
    for (auto& f : idleShow.fixtures)
    {
        const auto layout = ChoreographyEngine::detectPixelLayout (f);
        if (! layout.isPixelBar())
            continue;

        const float colVal = juce::jlimit (0.0f, 255.0f, colorOff * 255.0f);
        const float whiVal = juce::jlimit (0.0f, 255.0f, whiteOff * 255.0f);
        for (const auto& sec : layout.rgb)
            for (int ci : sec)
                f.channels[(size_t) ci].keyframes = { { 0.0, colVal, false } };
        for (int ci : layout.white)
            f.channels[(size_t) ci].keyframes = { { 0.0, whiVal, false } };
    }

    int maxUni = 0;
    for (const auto& f : idleShow.fixtures)
        maxUni = juce::jmax (maxUni, f.universe);
    idleShow.numUniverses = maxUni + 1;
    idleShow.valid = ! idleShow.fixtures.empty();
}

void AutomatorComponent::updateViewMode()
{
    const bool isStage = currentTab == 1;
    stage.setVisible (isStage && showStage);
    preview.setVisible (isStage && ! showStage);
    viewButton.setButtonText (showStage ? "Escenario" : "Niveles");
    perspectiveButton.setVisible (isStage && showStage);
}

void AutomatorComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        player.getDeviceManager(),
        0, 0,      // sin entradas (solo reproducimos)
        1, 2,      // salida mono/estereo
        false,     // sin MIDI in
        false,     // sin MIDI out
        true,      // canales como pares estereo
        false);    // muestra siempre las opciones avanzadas (frecuencia, buffer)
    selector->setSize (460, 360);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (selector.release());
    opts.dialogTitle = "Salida de audio";
    opts.dialogBackgroundColour = juce::Colour (P::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;

    if (auto* dw = opts.launchAsync())
        dw->centreAroundComponent (this, dw->getWidth(), dw->getHeight());

    // Persistimos la eleccion para la proxima vez (tambien se guarda al cerrar la app).
    player.saveDeviceState();
}

//==============================================================================
// Barra de menus tipo DAW.
//==============================================================================
juce::StringArray AutomatorComponent::getMenuBarNames()
{
    return { "Archivo", "Edicion", "Opciones", "Acerca de" };
}

juce::PopupMenu AutomatorComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;

    switch (topLevelMenuIndex)
    {
        case 0: // Archivo
            m.addItem (1, "Anadir temas...");
            m.addSeparator();
            m.addItem (3, "Proyecto nuevo");
            m.addItem (4, "Abrir proyecto...");
            m.addItem (5, "Guardar proyecto");
            m.addItem (6, "Guardar proyecto como...");
            {
                juce::PopupMenu recents;
                const auto files = playlist.getRecentProjects();
                for (int i = 0; i < files.size(); ++i)
                    recents.addItem (600 + i, files[i].getFileNameWithoutExtension());
                if (files.size() > 0)
                {
                    recents.addSeparator();
                    recents.addItem (699, "Limpiar recientes");
                }
                m.addSubMenu ("Proyectos recientes", recents, files.size() > 0);
            }
            m.addSeparator();
            m.addItem (2, "Salir");
            break;

        case 1: // Edicion
            m.addItem (10, "Quitar tema", list.getSelectedRow() >= 0);
            m.addItem (11, "Subir",       list.getSelectedRow() > 0);
            m.addItem (12, "Bajar",       list.getSelectedRow() >= 0 && list.getSelectedRow() < playlist.size() - 1);
            m.addSeparator();
            m.addItem (13, "Reentrenar stems...", list.getSelectedRow() >= 0 && playlist.trackHasStems (list.getSelectedRow()));
            break;

        case 2: // Opciones
            m.addItem (20, "Salida de audio...");
            m.addSeparator();
            m.addItem (22, "Configurar salida DMX...");
            m.addSeparator();
            m.addItem (23, "Preferencias...");
            break;

        case 3: // Acerca de
            m.addItem (30, "Acerca de LuxSync");
            break;

        default:
            break;
    }
    return m;
}

void AutomatorComponent::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case 1:  addFilesDialog(); break;
        case 2:  juce::JUCEApplicationBase::quit(); break;

        case 3:  newProjectAction(); break;
        case 4:  openProjectAction(); break;
        case 5:  saveProjectAction(); break;
        case 6:  saveProjectAsAction(); break;

        case 10: removeSelected(); break;
        case 11: moveSelected (-1); break;
        case 12: moveSelected (+1); break;
        case 13: retrainSelected(); break;

        case 20: showAudioSettings(); break;
        case 22: setTab (6); break;
        case 23: showPreferences(); break;

        case 30: showAboutDialog(); break;

        default:
            if (menuItemID >= 600 && menuItemID < 699)
            {
                const auto files = playlist.getRecentProjects();
                const int idx = menuItemID - 600;
                if (idx >= 0 && idx < files.size())
                    openProjectPath (files[idx]);
            }
            else if (menuItemID == 699)
            {
                playlist.clearRecentProjects();
            }
            break;
    }
}

void AutomatorComponent::showAboutDialog()
{    juce::NativeMessageBox::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon,
        "Acerca de LuxSync",
        "LuxSync  ·  AI Automator\n\n"
        "Generador automatico de espectaculos de luces DMX sincronizados con la musica.\n"
        "Analiza cada tema (y opcionalmente separa stems con IA) para crear coreografias "
        "de luz por instrumento.\n\n"
        "Salidas: Art-Net, sACN y Enttec / FTDI (Open DMX y USB Pro).\n\n"
        "(c) 2026 LuxSync",
        this);
}

void AutomatorComponent::showPreferences()
{
    auto panel = std::make_unique<PreferencesPanel> (playlist);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel.release());
    opts.dialogTitle = "Preferencias";
    opts.dialogBackgroundColour = juce::Colour (LuxLookAndFeel::Palette::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

std::vector<PlaybackEngine::Universe> AutomatorComponent::buildTestFrame() const
{
    // Valor de prueba por tipo de canal: enciende lo visible, sin strobe ni
    // movimiento brusco, para comprobar que la direccion DMX llega bien.
    auto testValue = [] (ChannelType t) -> juce::uint8
    {
        switch (t)
        {
            case ChannelType::Dimmer:
            case ChannelType::Red:
            case ChannelType::Green:
            case ChannelType::Blue:
            case ChannelType::White:
            case ChannelType::Amber:
            case ChannelType::UV:
            case ChannelType::Shutter:   // normalmente 255 = abierto
                return 255;
            case ChannelType::Pan:
            case ChannelType::Tilt:
                return 128;              // centrado
            default:
                return 0;                // Strobe/Color/Gobo/Generic/finos/Zoom/Focus
        }
    };

    const auto& rig = playlist.getRig();
    int maxUni = 0;
    for (const auto& f : rig)
        maxUni = juce::jmax (maxUni, f.universe);

    std::vector<PlaybackEngine::Universe> frame ((size_t) maxUni + 1);
    for (auto& u : frame) u.fill (0);

    for (const auto& f : rig)
    {
        for (int ci = 0; ci < f.channelCount(); ++ci)
        {
            const int addr = f.dmxAddressOf (ci);   // 1..512
            if (addr < 1 || addr > 512)
                continue;
            frame[(size_t) f.universe][(size_t) (addr - 1)] = testValue (f.channels[(size_t) ci].type);
        }
    }
    return frame;
}

void AutomatorComponent::toggleTest()
{
    testActive = testButton.getToggleState();
    testButton.setButtonText (testActive ? "Probando" : "Probar");

    if (! testActive)
        engine.blackout();   // al salir de la prueba, apaga todo
}

void AutomatorComponent::selectedRowsChanged (int)
{
    updateRetrainEnabled();
}

void AutomatorComponent::updateRetrainEnabled()
{
    const int row = list.getSelectedRow();
    retrainButton.setEnabled (row >= 0 && playlist.trackHasStems (row));
    colorsButton.setEnabled (row >= 0);
    songChoreoButton.setEnabled (row >= 0);
}

void AutomatorComponent::editPreferredColors()
{
    const int row = list.getSelectedRow();
    if (row < 0 || row >= playlist.size())
        return;

    auto panel = std::make_unique<PreferredColorsPanel> (playlist.getPreferredColors (row));
    panel->onApply = [this, row] (const std::vector<juce::Colour>& cols)
    {
        playlist.setPreferredColors (row, cols);
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel.release());
    opts.dialogTitle = "Colores preferidos - " + playlist.getTrack (row).displayName;
    opts.dialogBackgroundColour = juce::Colour (LuxLookAndFeel::Palette::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

int AutomatorComponent::trackIndexForFile (const juce::File& f) const
{
    for (int i = 0; i < playlist.size(); ++i)
        if (playlist.getTrack (i).file == f)
            return i;
    return -1;
}

void AutomatorComponent::editPreferredColorsForFile (const juce::File& f)
{
    int row = trackIndexForFile (f);
    if (row < 0) row = playlist.addFile (f);   // lo incorpora al motor si aun no estaba
    if (row < 0 || row >= playlist.size())
        return;

    auto panel = std::make_unique<PreferredColorsPanel> (playlist.getPreferredColors (row));
    panel->onApply = [this, row] (const std::vector<juce::Colour>& cols)
    {
        playlist.setPreferredColors (row, cols);
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel.release());
    opts.dialogTitle = "Colores preferidos - " + playlist.getTrack (row).displayName;
    opts.dialogBackgroundColour = juce::Colour (LuxLookAndFeel::Palette::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void AutomatorComponent::showSongProperties (const juce::File& f)
{
    int row = trackIndexForFile (f);
    if (row < 0) row = playlist.addFile (f);   // lo incorpora si aun no estaba
    if (row < 0 || row >= playlist.size())
        return;

    auto panel = std::make_unique<SongPropertiesPanel> (playlist, row);
    panel->onChanged = [this]
    {
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    panel->onEditManual = [this] (int pi) { openManualEditor (pi); };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel.release());
    opts.dialogTitle = "Propiedades - " + playlist.getTrack (row).displayName;
    opts.dialogBackgroundColour = juce::Colour (LuxLookAndFeel::Palette::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void AutomatorComponent::editSongChoreosForFile (const juce::File& f,
                                                 const juce::StringArray& playlistPaths,
                                                 const juce::String& playlistName)
{
    int row = trackIndexForFile (f);
    if (row < 0) row = playlist.addFile (f);
    if (row < 0 || row >= playlist.size())
        return;

    if (playlist.getChoreoLibrary().empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Coreografias",
            "Aun no hay coreografias en la libreria. Crea alguna en la pestana \"Creador\".");
        return;
    }

    const auto& track = playlist.getTrack (row);
    juce::StringArray paths = playlistPaths;
    if (paths.isEmpty())
        paths.add (f.getFullPathName());

    auto panel = std::make_unique<SongChoreoPanel> (playlist,
                                                    f.getFullPathName(),
                                                    track.displayName,
                                                    paths,
                                                    playlistName.isEmpty() ? juce::String ("Biblioteca") : playlistName);
    panel->onChanged = [this]
    {
        rebuildIdleShow();
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel.release());
    opts.dialogTitle = "Coreografias - " + track.displayName;
    opts.dialogBackgroundColour = juce::Colour (LuxLookAndFeel::Palette::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

void AutomatorComponent::openManualEditor (int playlistIndex)
{
    if (playlistIndex < 0 || playlistIndex >= playlist.size())
        return;

    // Lleva a la pestana Piano roll y selecciona el tema. El propio panel hace la
    // pregunta de creacion (hornear / en blanco) si aun no tiene coreografia manual.
    setTab (6);
    if (pianoPanel != nullptr)
        pianoPanel->selectSong (playlistIndex);
}

void AutomatorComponent::launchManualEditorWindow (int playlistIndex)
{
    auto* showPtr = playlist.manualShowRef (playlistIndex);
    if (showPtr == nullptr)
        return;

    // Asegura que el modo activo es Manual para previsualizar las ediciones.
    playlist.setChoreoMode (playlistIndex, Track::ChoreoMode::Manual);

    const auto name = playlist.getTrack (playlistIndex).displayName;
    auto editor = std::make_unique<ManualShowEditor> (*showPtr, name);

    editor->onChanged = [this, playlistIndex]
    {
        playlist.manualShowEdited (playlistIndex);
        updateActiveShow();
        if (showStage) stage.repaint(); else preview.repaint();
    };
    editor->getPlayheadSeconds = [this, playlistIndex] () -> double
    {
        return (currentlyPlaying == playlistIndex && player.hasFile())
                 ? player.getPositionSeconds() : -1.0;
    };
    editor->isPlaying = [this, playlistIndex] () -> bool
    {
        return currentlyPlaying == playlistIndex && player.isPlaying();
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (editor.release());
    opts.dialogTitle = "Piano roll - " + name;
    opts.dialogBackgroundColour = juce::Colour (LuxLookAndFeel::Palette::bg1);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

namespace
{
    // Contenido de la ventana aparte: el escenario + un boton 2.5D/2D para
    // cambiar la perspectiva sin salir de la ventana.
    class StagePopoutContent : public juce::Component
    {
    public:
        StagePopoutContent()
        {
            using P = LuxLookAndFeel::Palette;
            addAndMakeVisible (stage);

            perspectiveButton.setButtonText (stage.isPerspective() ? "2.5D" : "2D");
            perspectiveButton.setClickingTogglesState (true);
            perspectiveButton.setToggleState (stage.isPerspective(), juce::dontSendNotification);
            perspectiveButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (P::accent));
            perspectiveButton.setColour (juce::TextButton::textColourOnId,  juce::Colour (P::bg0));
            perspectiveButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::textMid));
            perspectiveButton.setTooltip ("Cambia entre vista en perspectiva (2.5D) y vista plana (2D).");
            perspectiveButton.onClick = [this]
            {
                const bool on = perspectiveButton.getToggleState();
                stage.setPerspective (on);
                perspectiveButton.setButtonText (on ? "2.5D" : "2D");
            };
            addAndMakeVisible (perspectiveButton);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto bar = area.removeFromTop (34).reduced (8, 5);
            perspectiveButton.setBounds (bar.removeFromRight (70));
            stage.setBounds (area);
        }

        StageVisualizer stage;
        juce::TextButton perspectiveButton;
    };

    // Ventana flotante que aloja el escenario; al cerrarla avisa al padre.
    class StagePopoutWindow : public juce::DocumentWindow
    {
    public:
        explicit StagePopoutWindow (std::function<void()> onCloseCb)
            : juce::DocumentWindow ("Escenario",
                                    juce::Colour (LuxLookAndFeel::Palette::bg0),
                                    juce::DocumentWindow::closeButton),
              closeCb (std::move (onCloseCb))
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
        }

        void closeButtonPressed() override { if (closeCb) closeCb(); }

    private:
        std::function<void()> closeCb;
    };
}

void AutomatorComponent::openStageWindow()
{
    if (stageWindow != nullptr)
    {
        stageWindow->toFront (true);
        return;
    }

    auto* content = new StagePopoutContent();
    content->stage.setMoveFigure (playlist.getMoveFigureIndex());
    content->stage.setPerspective (stage.isPerspective());
    content->perspectiveButton.setToggleState (stage.isPerspective(), juce::dontSendNotification);
    content->perspectiveButton.setButtonText (stage.isPerspective() ? "2.5D" : "2D");
    content->setSize (900, 560 + 34);
    popoutStage = &content->stage;

    auto* w = new StagePopoutWindow ([this] { closeStageWindow(); });
    w->setContentOwned (content, true);
    w->centreWithSize (content->getWidth(), content->getHeight() + w->getTitleBarHeight());
    w->setVisible (true);
    stageWindow.reset (w);

    updateActiveShow();   // fija el show actual tambien en la ventana aparte
}

void AutomatorComponent::closeStageWindow()
{
    popoutStage = nullptr;
    juce::MessageManager::callAsync ([this] { stageWindow.reset(); });
}

void AutomatorComponent::retrainSelected()
{
    const int row = list.getSelectedRow();
    if (row < 0 || ! playlist.trackHasStems (row))
        return;

    const auto name = playlist.getTrack (row).displayName;
    juce::NativeMessageBox::showYesNoBox (
        juce::MessageBoxIconType::QuestionIcon,
        "Reentrenar stems",
        "Vas a reentrenar los stems de:\n\n" + name
            + "\n\nSe ignorara la cache y la IA volvera a separar el tema (puede tardar). "
              "Estas seguro de que quieres reentrenar los Stems?",
        this,
        juce::ModalCallbackFunction::create ([this, row] (int result)
        {
            if (result == 1)
            {
                playlist.retrainTrack (row);
                updateRetrainEnabled();
            }
        }));
}

void AutomatorComponent::updateActiveShow()
{
    const DmxShow* s = idleShow.valid ? &idleShow : nullptr;

    if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
    {
        if (const auto* active = playlist.activeShowFor (currentlyPlaying))
            s = active;
    }

    engine.setActiveShow (s);
    preview.setShow (s);
    stage.setShow (s);
    if (popoutStage != nullptr) popoutStage->setShow (s);

    // Estructura del tema activo en la franja de secciones.
    if (currentlyPlaying >= 0 && currentlyPlaying < playlist.size())
    {
        const auto& a = playlist.getTrack (currentlyPlaying).analysis;
        if (a.valid && ! a.sections.empty())
            sectionBar.setSections (a.sections, a.lengthSeconds);
        else
            sectionBar.clearSections();
    }
    else
    {
        sectionBar.clearSections();
    }
}

//==============================================================================
void AutomatorComponent::addFilesDialog()
{
    auto startDir = playlist.getMusicFolder();
    if (! startDir.isDirectory())
        startDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> (
        "Anade temas a la playlist", startDir, playlist.getSupportedWildcard());

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto results = fc.getResults();
        if (! results.isEmpty())
            playlist.addFiles (results);
    });
}

//==============================================================================
// Proyectos .lux

void AutomatorComponent::updateWindowTitle()
{
    const auto proj = playlist.getCurrentProjectFile();
    juce::String title = "LuxSync AI Automator";
    if (proj.existsAsFile())
        title += "  -  " + proj.getFileNameWithoutExtension();

    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        w->setName (title);
}

void AutomatorComponent::newProjectAction()
{
    juce::NativeMessageBox::showYesNoBox (
        juce::MessageBoxIconType::QuestionIcon,
        "Proyecto nuevo",
        "Vas a empezar un proyecto nuevo. Se quitaran los temas y sus coreografias "
        "(el rig de equipos se conserva).\n\nGuarda antes si quieres conservar el proyecto actual. "
        "Continuar?",
        this,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 1)
            {
                player.stop();
                currentlyPlaying = -1;
                playlist.newProject();
                updateWindowTitle();
            }
        }));
}

void AutomatorComponent::openProjectAction()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Abrir proyecto LuxSync", juce::File(), "*.lux");

    const auto openFlags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (openFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            openProjectPath (file);
    });
}

void AutomatorComponent::openProjectPath (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Abrir proyecto",
            "No se encontro el archivo:\n" + file.getFullPathName(), this);
        return;
    }

    player.stop();
    currentlyPlaying = -1;

    if (! playlist.openProjectFile (file))
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Abrir proyecto",
            "No se pudo leer el proyecto:\n" + file.getFullPathName(), this);
        return;
    }
    updateWindowTitle();
}

void AutomatorComponent::saveProjectAction()
{
    const auto cur = playlist.getCurrentProjectFile();
    if (cur.getFullPathName().isNotEmpty())
    {
        playlist.saveProjectToFile (cur);
        updateWindowTitle();
    }
    else
    {
        saveProjectAsAction();
    }
}

void AutomatorComponent::saveProjectAsAction()
{
    auto start = playlist.getCurrentProjectFile();
    if (start.getFullPathName().isEmpty())
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                    .getChildFile ("Mi show.lux");

    chooser = std::make_unique<juce::FileChooser> (
        "Guardar proyecto LuxSync", start, "*.lux");

    const auto saveFlags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (saveFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.getFullPathName().isEmpty())
            return;

        if (playlist.saveProjectToFile (file))
            updateWindowTitle();
        else
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon, "Guardar proyecto",
                "No se pudo guardar el proyecto.", this);
    });
}

void AutomatorComponent::removeSelected()
{
    const int row = list.getSelectedRow();
    if (row >= 0)
        playlist.removeTrack (row);
}

void AutomatorComponent::moveSelected (int delta)
{
    const int row = list.getSelectedRow();
    if (row < 0)
        return;

    const int newRow = playlist.moveTrack (row, delta);
    list.selectRow (newRow);

    if (currentlyPlaying == row)            currentlyPlaying = newRow;
    else if (currentlyPlaying == newRow)    currentlyPlaying = row;
}

void AutomatorComponent::playSelected()
{
    int row = list.getSelectedRow();
    if (row < 0 && playlist.size() > 0)
        row = 0;
    playTrack (row);
}

bool AutomatorComponent::keyPressed (const juce::KeyPress& key)
{
    // Barra espaciadora: reproduce / pausa la reproduccion del Reproductor.
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayPause();
        return true;
    }

    // B o Esc: apagon de emergencia (alterna el blackout).
    if (key.getTextCharacter() == 'b' || key.getTextCharacter() == 'B'
        || key == juce::KeyPress::escapeKey)
    {
        blackoutButton.setToggleState (! blackoutButton.getToggleState(), juce::sendNotification);
        return true;
    }

    return false;
}

void AutomatorComponent::togglePlayPause()
{
    if (player.isPlaying())
    {
        player.pause();
        return;
    }

    // Reanudar: si hay un tema cargado pero no hay show activo (p.ej. tras Stop),
    // reasegura que el lightshow se active para ESTE tema.
    if (player.hasFile() && currentlyPlaying >= 0)
    {
        player.play();
        updateActiveShow();   // garantiza que el show del tema este activo
        return;
    }

    // No hay nada cargado todavia: arranca el tema seleccionado (o el primero).
    playSelected();
}

void AutomatorComponent::playTrack (int index)
{
    if (index < 0 || index >= playlist.size())
        return;

    const auto& t = playlist.getTrack (index);
    if (t.state == Track::State::Error)
        return;

    if (player.load (t.file))
    {
        player.play();
        currentlyPlaying = index;
        list.selectRow (index);
        list.repaint();
        updateActiveShow();
    }
}

void AutomatorComponent::playNext()
{
    // Avanza dentro de la cola visible respetando aleatorio / repetir.
    if (playOrder.empty())
    {
        stopPlayback();
        return;
    }

    ++orderPos;
    if (orderPos >= (int) playOrder.size())
    {
        if (! repeatMode)
        {
            stopPlayback();
            return;
        }
        // Repetir: reinicia la lista. Si ademas hay aleatorio, baraja de nuevo.
        if (shuffleMode)
            rebuildPlayOrder (-1);
        orderPos = 0;
    }
    playOrderPos();
}

//==============================================================================
// Cola de reproduccion: sigue el orden visible del Reproductor (arriba->abajo),
// con soporte de aleatorio (sin repetir hasta agotar) y repetir lista.

void AutomatorComponent::startQueue (const juce::Array<juce::File>& files, int startIndex)
{
    playQueue = files;
    if (playQueue.isEmpty())
    {
        stopPlayback();
        return;
    }
    if (startIndex < 0 || startIndex >= playQueue.size())
        startIndex = 0;

    rebuildPlayOrder (startIndex);   // deja orderPos apuntando al tema inicial
    playOrderPos();
}

void AutomatorComponent::rebuildPlayOrder (int startFileIndex)
{
    const int n = playQueue.size();
    playOrder.clear();
    if (n == 0)
        return;

    if (! shuffleMode)
    {
        // Orden natural; arranca en startFileIndex si se indico.
        for (int i = 0; i < n; ++i)
            playOrder.push_back (i);
        orderPos = (startFileIndex >= 0 && startFileIndex < n) ? startFileIndex : 0;
        return;
    }

    // Aleatorio: permutacion sin repeticiones. Si se indico inicio, va primero.
    std::vector<int> rest;
    for (int i = 0; i < n; ++i)
        if (i != startFileIndex)
            rest.push_back (i);

    for (int i = (int) rest.size() - 1; i > 0; --i)
        std::swap (rest[(size_t) i], rest[(size_t) shuffleRng.nextInt (i + 1)]);

    if (startFileIndex >= 0 && startFileIndex < n)
        playOrder.push_back (startFileIndex);
    for (int idx : rest)
        playOrder.push_back (idx);
    orderPos = 0;
}

void AutomatorComponent::playOrderPos()
{
    if (orderPos < 0 || orderPos >= (int) playOrder.size())
        return;
    const int fileIdx = playOrder[(size_t) orderPos];
    if (fileIdx < 0 || fileIdx >= playQueue.size())
        return;

    const auto file = playQueue[fileIdx];
    int idx = trackIndexForFile (file);
    if (idx < 0)
        idx = playlist.addFile (file);
    if (idx >= 0)
        playTrack (idx);
}

void AutomatorComponent::stopPlayback()
{
    player.stop();
    currentlyPlaying = -1;
    orderPos = -1;
    list.repaint();
    updateActiveShow();
    engine.blackout();
}


juce::String AutomatorComponent::formatTime (double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;
    const int total = (int) seconds;
    return juce::String (total / 60).paddedLeft ('0', 2)
         + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}
