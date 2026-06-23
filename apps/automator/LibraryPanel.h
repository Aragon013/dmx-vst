#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../source/LuxLookAndFeel.h"
#include "MusicLibrary.h"
#include "PlaylistManager.h"
#include <set>

/**
    Pantalla "Reproductor" (Fase 3): biblioteca de musica al estilo Apple Music
    FUSIONADA con el reproductor. Es la unica pestana de reproduccion.

    - Barra lateral con "Biblioteca" + playlists del usuario (crear / borrar / renombrar).
    - Lista principal con portada, titulo, artista, album, duracion, indicador de
      reproduccion y estado de analisis (del PlaylistManager).
    - Buscador por texto (titulo / artista / album).
    - Banner "Ahora suena" con portada del tema en reproduccion.
    - Anadir carpetas o archivos a la biblioteca (escaneo manual).
    - Anadir temas a una playlist (boton + menu contextual + dialogo de seleccion).
    - Controles de show del tema seleccionado: Estilo, Colores, Coreografias.
    - Doble clic / "Reproducir" arranca el tema (sin cambiar de pestana).
*/
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    using P = LuxLookAndFeel::Palette;

    // Reproduccion / show (los implementa AutomatorComponent).
    std::function<void (const juce::Array<juce::File>&, int)> onPlayQueue;   // cola en orden visible + indice inicial
    std::function<void (const juce::Array<juce::File>&)>      onTrainFiles;  // entrena (analiza) estos archivos
    std::function<int ()>                   onRegenerate;  // recalcula las secuencias de luces (rapido); devuelve nº de temas
    std::function<void (bool)>              onShuffleChanged;
    std::function<void (bool)>              onRepeatChanged;
    std::function<void (const juce::File&)> onEditColors;
    std::function<void (const juce::File&, const juce::StringArray&, const juce::String&)> onEditChoreos;
    std::function<void (int)>               onEditManual;   // abre el piano-roll manual (indice de playlist)
    std::function<void (const juce::File&)> onShowProperties;   // abre la ventana de propiedades del tema
    std::function<void ()>                  onStyleChanged;
    std::function<juce::File ()>            getNowPlayingFile;

    LibraryPanel (MusicLibrary& lib, PlaylistManager& pm) : library (lib), playlist (pm)
    {
        title.setText ("Reproductor", juce::dontSendNotification);
        title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        search.setTextToShowWhenEmpty ("Buscar titulo, artista, album...", juce::Colour (P::textDim));
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (P::control));
        search.setColour (juce::TextEditor::outlineColourId, juce::Colour (P::line));
        search.onTextChange = [this] { rebuildVisible(); trackList.updateContent(); repaint(); };
        addAndMakeVisible (search);

        addFolderButton.setButtonText ("+ Carpeta");
        addFolderButton.setTooltip ("Escanea una carpeta (y subcarpetas) y anade los temas a la biblioteca.");
        addFolderButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (addFolderButton);

        addFilesButton.setButtonText ("+ Archivos");
        addFilesButton.setTooltip ("Anade archivos de musica sueltos a la biblioteca.");
        addFilesButton.onClick = [this] { chooseFiles(); };
        addAndMakeVisible (addFilesButton);

        // --- controles de show del tema seleccionado ---
        styleLabel.setText ("Estilo:", juce::dontSendNotification);
        styleLabel.setFont (juce::FontOptions (12.0f));
        styleLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        styleLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (styleLabel);

        styleCombo.setWantsKeyboardFocus (false);
        styleCombo.setTooltip ("Dinamica de patron/color de las luces: chase, alterno, onda, arcoiris, pulso...");
        {
            const auto names = playlist.getStyleNames();
            for (int i = 0; i < names.size(); ++i)
                styleCombo.addItem (names[i], i + 1);
        }
        styleCombo.setSelectedId (playlist.getStyleIndex() + 1, juce::dontSendNotification);
        styleCombo.onChange = [this]
        {
            playlist.setStyleIndex (styleCombo.getSelectedId() - 1);
            if (onStyleChanged) onStyleChanged();
        };
        addAndMakeVisible (styleCombo);

        // Coreografia de MOVIMIENTO de los fixtures con motor (cabezas/spiders).
        moveLabel.setText ("Movimiento:", juce::dontSendNotification);
        moveLabel.setFont (juce::FontOptions (12.0f));
        moveLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        moveLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (moveLabel);

        moveCombo.setWantsKeyboardFocus (false);
        moveCombo.setTooltip ("Figura de movimiento de las luces con motor: barrido sync, espejo, cruce, circulos... Auto IA la elige segun el momento.");
        {
            const auto names = playlist.getMoveFigureNames();
            for (int i = 0; i < names.size(); ++i)
                moveCombo.addItem (names[i], i + 1);
        }
        moveCombo.setSelectedId (playlist.getMoveFigureIndex() + 1, juce::dontSendNotification);
        moveCombo.onChange = [this]
        {
            playlist.setMoveFigureIndex (moveCombo.getSelectedId() - 1);
            if (onStyleChanged) onStyleChanged();
        };
        addAndMakeVisible (moveCombo);

        choreosButton.setButtonText ("Coreografias...");
        choreosButton.setTooltip ("Coreografias del tema seleccionado (el Auto IA las reparte por energia).");
        choreosButton.onClick = [this]
        {
            auto f = selectedFile();
            if (f.existsAsFile() && onEditChoreos)
                onEditChoreos (f, currentPlaylistPaths(), currentPlaylistName());
        };
        addAndMakeVisible (choreosButton);

        // --- aleatorio / repetir ---
        shuffleToggle.setButtonText ("Aleatorio");
        shuffleToggle.setTooltip ("Reproduce la lista en orden aleatorio sin repetir hasta agotarla.");
        shuffleToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (P::textMid));
        shuffleToggle.onClick = [this] { if (onShuffleChanged) onShuffleChanged (shuffleToggle.getToggleState()); };
        addAndMakeVisible (shuffleToggle);

        repeatToggle.setButtonText ("Repetir");
        repeatToggle.setTooltip ("Al terminar la lista, vuelve a empezar (si tambien hay aleatorio, baraja de nuevo).");
        repeatToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (P::textMid));
        repeatToggle.onClick = [this] { if (onRepeatChanged) onRepeatChanged (repeatToggle.getToggleState()); };
        addAndMakeVisible (repeatToggle);

        // --- barra lateral (playlists) ---
        playlistModel.owner = this;
        sidebar.setModel (&playlistModel);
        sidebar.setRowHeight (30);
        sidebar.setColour (juce::ListBox::backgroundColourId, juce::Colour (P::bg1));
        sidebar.setColour (juce::ListBox::outlineColourId, juce::Colour (P::line));
        sidebar.setOutlineThickness (1);
        addAndMakeVisible (sidebar);

        newListButton.setButtonText ("+ Lista");
        newListButton.onClick = [this] { createPlaylist(); };
        addAndMakeVisible (newListButton);

        delListButton.setButtonText ("Borrar");
        delListButton.onClick = [this] { deleteSelectedPlaylist(); };
        addAndMakeVisible (delListButton);

        // --- lista de temas ---
        trackList.setModel (this);
        trackList.setRowHeight (50);
        trackList.setMultipleSelectionEnabled (true);
        trackList.setColour (juce::ListBox::backgroundColourId, juce::Colour (P::bg0));
        trackList.setColour (juce::ListBox::outlineColourId, juce::Colour (P::line));
        trackList.setOutlineThickness (1);
        addAndMakeVisible (trackList);

        // Cabecera de la lista con casilla "Seleccionar todo" (solo temas visibles).
        {
            auto header = std::make_unique<ListHeader> (*this);
            header->setSize (10, 28);   // ListBox usa la altura; el ancho lo ajusta el
            listHeader = header.get();  //               propio ListBox.
            trackList.setHeaderComponent (std::move (header));
        }

        playButton.setButtonText ("Reproducir");
        playButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a6a2a));
        playButton.onClick = [this] { playSelected(); };
        addAndMakeVisible (playButton);

        trainButton.setButtonText ("Generar Stems");
        trainButton.setColour (juce::TextButton::buttonColourId, juce::Colour (P::accent));
        trainButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::bg0));
        trainButton.setTooltip ("Genera (separa) los instrumentos/stems con IA de los temas con la "
                                "casilla marcada (y analiza secciones). Marca varias casillas o usa "
                                "\"Seleccionar todo\" arriba de la lista. Tarda varios minutos; se cachea.");
        trainButton.onClick = [this] { trainAction(); };
        addAndMakeVisible (trainButton);

        regenButton.setButtonText ("Generar Secuencias");
        regenButton.setTooltip ("Recalcula las secuencias de luces de todos los temas ya analizados. "
                                "Es INSTANTANEO (no vuelve a generar stems). Usalo tras cambiar equipos, "
                                "estilo, colores o el motor de luces.");
        regenButton.onClick = [this]
        {
            const int n = onRegenerate ? onRegenerate() : 0;
            flashStatus (n > 0 ? ("Secuencias generadas: " + juce::String (n)
                                  + (n == 1 ? " tema" : " temas"))
                               : "No hay temas analizados. Usa \"Generar Stems\" primero.");
        };
        addAndMakeVisible (regenButton);

        addToListButton.setButtonText ("Anadir a lista");
        addToListButton.setTooltip ("Anade los temas seleccionados a una de tus listas.");
        addToListButton.onClick = [this] { showAddToListMenu(); };
        addAndMakeVisible (addToListButton);

        addTracksButton.setButtonText ("Anadir pistas...");
        addTracksButton.setTooltip ("Elige temas de la biblioteca para anadirlos a esta lista.");
        addTracksButton.onClick = [this] { openAddTracksDialog(); };
        addAndMakeVisible (addTracksButton);

        removeButton.setButtonText ("Quitar");
        removeButton.onClick = [this] { removeSelected(); };
        addAndMakeVisible (removeButton);

        status.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        status.setFont (juce::FontOptions (12.0f));
        status.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (status);

        library.addChangeListener (this);
        playlist.addChangeListener (this);
        sidebar.selectRow (0);
        rebuildVisible();
        updateButtons();
        startTimerHz (8);
    }

    ~LibraryPanel() override
    {
        library.removeChangeListener (this);
        playlist.removeChangeListener (this);
        stopTimer();
    }

    void refreshFromLibrary()
    {
        styleCombo.setSelectedId (playlist.getStyleIndex() + 1, juce::dontSendNotification);
        rebuildVisible();
        sidebar.updateContent();
        trackList.updateContent();
        updateButtons();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (P::bg0));

        // --- banner "Ahora suena" ---
        auto b = bannerBounds.toFloat();
        g.setColour (juce::Colour (P::surface));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (juce::Colour (P::line));
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

        juce::File npf = getNowPlayingFile ? getNowPlayingFile() : juce::File();
        auto inner = bannerBounds.reduced (10);
        auto art = inner.removeFromLeft (inner.getHeight());

        g.setColour (juce::Colour (P::control));
        g.fillRoundedRectangle (art.toFloat(), 5.0f);

        if (npf.existsAsFile())
        {
            const int idx = library.indexForPath (npf.getFullPathName());
            auto cover = idx >= 0 ? library.getArtwork (idx) : juce::Image();
            if (cover.isValid())
            {
                g.saveState();
                juce::Path clip; clip.addRoundedRectangle (art.toFloat(), 5.0f);
                g.reduceClipRegion (clip);
                g.drawImage (cover, art.toFloat(),
                             juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
                g.restoreState();
            }
            else
                drawNoteGlyph (g, art);

            inner.removeFromLeft (12);

            juce::String tTitle = npf.getFileNameWithoutExtension();
            juce::String tArtist;
            if (idx >= 0)
            {
                const auto& it = library.items()[(size_t) idx];
                tTitle  = it.displayTitle();
                tArtist = it.artist;
            }

            g.setColour (juce::Colour (P::accent));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("AHORA SUENA", inner.removeFromTop (14), juce::Justification::bottomLeft, false);

            g.setColour (juce::Colour (P::textHi));
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (tTitle, inner.removeFromTop (22), juce::Justification::centredLeft, true);

            g.setColour (juce::Colour (P::textMid));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (tArtist.isNotEmpty() ? tArtist : "Artista desconocido",
                        inner.removeFromTop (16), juce::Justification::centredLeft, true);
        }
        else
        {
            drawNoteGlyph (g, art);
            inner.removeFromLeft (12);
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText ("Nada en reproduccion - elige un tema y pulsa Reproducir",
                        inner, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);

        auto top = r.removeFromTop (30);
        title.setBounds (top.removeFromLeft (170));
        search.setBounds (top.removeFromRight (280));
        r.removeFromTop (10);

        auto toolbar = r.removeFromTop (30);
        addFolderButton.setBounds (toolbar.removeFromLeft (110));
        toolbar.removeFromLeft (8);
        addFilesButton.setBounds (toolbar.removeFromLeft (110));
        toolbar.removeFromLeft (16);
        shuffleToggle.setBounds (toolbar.removeFromLeft (100));
        toolbar.removeFromLeft (4);
        repeatToggle.setBounds (toolbar.removeFromLeft (90));
        // controles de show a la derecha
        choreosButton.setBounds (toolbar.removeFromRight (130));
        toolbar.removeFromRight (12);
        styleCombo.setBounds (toolbar.removeFromRight (160));
        styleLabel.setBounds (toolbar.removeFromRight (48));
        toolbar.removeFromRight (10);
        moveCombo.setBounds (toolbar.removeFromRight (150));
        moveLabel.setBounds (toolbar.removeFromRight (76));
        r.removeFromTop (12);

        bannerBounds = r.removeFromTop (66);
        r.removeFromTop (12);

        // barra lateral
        auto side = r.removeFromLeft (190);
        auto sideButtons = side.removeFromBottom (30);
        newListButton.setBounds (sideButtons.removeFromLeft (90));
        sideButtons.removeFromLeft (6);
        delListButton.setBounds (sideButtons.removeFromLeft (80));
        side.removeFromBottom (8);
        sidebar.setBounds (side);

        r.removeFromLeft (14);

        // acciones bajo la lista
        auto actions = r.removeFromBottom (34);
        playButton.setBounds (actions.removeFromLeft (110));
        actions.removeFromLeft (8);
        trainButton.setBounds (actions.removeFromLeft (120));
        actions.removeFromLeft (8);
        regenButton.setBounds (actions.removeFromLeft (150));
        actions.removeFromLeft (12);
        addToListButton.setBounds (actions.removeFromLeft (120));
        actions.removeFromLeft (8);
        addTracksButton.setBounds (actions.removeFromLeft (130));
        actions.removeFromLeft (8);
        removeButton.setBounds (actions.removeFromLeft (100));
        status.setBounds (actions.removeFromRight (150));
        r.removeFromBottom (8);
        trackList.setBounds (r);
    }

private:
    //==============================================================================
    static void drawNoteGlyph (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (juce::Colour (P::textDim));
        const auto c = area.toFloat().getCentre();
        juce::Path note;
        note.addEllipse (c.x - 6.0f, c.y + 2.0f, 6.0f, 6.0f);
        note.addRectangle (c.x - 0.6f, c.y - 9.0f, 1.6f, 11.0f);
        note.addRectangle (c.x - 0.6f, c.y - 9.0f, 6.0f, 1.6f);
        g.fillPath (note);
    }

    //==============================================================================
    // ListBoxModel de la lista principal.

    int getNumRows() override { return (int) visible.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) visible.size()) return;
        const int catIdx = visible[(size_t) row];
        const auto& items = library.items();
        if (catIdx < 0 || catIdx >= (int) items.size()) return;
        const auto& it = items[(size_t) catIdx];

        const bool isPlaying = nowPlayingPath.isNotEmpty()
                            && it.file.getFullPathName() == nowPlayingPath;

        if (selected)            g.fillAll (juce::Colour (P::accent).withAlpha (0.16f));
        else if (isPlaying)      g.fillAll (juce::Colour (0xff62d96b).withAlpha (0.10f));
        else if (row % 2 == 1)   g.fillAll (juce::Colours::white.withAlpha (0.018f));

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 5);

        // casilla de seleccion para "Entrenar todo"
        auto checkBox = area.removeFromLeft (24).withSizeKeepingCentre (18, 18);
        const bool checked = checkedPaths.count (it.file.getFullPathName()) > 0;
        g.setColour (juce::Colour (checked ? P::accent : P::line));
        g.drawRoundedRectangle (checkBox.toFloat(), 3.0f, 1.4f);
        if (checked)
        {
            g.setColour (juce::Colour (P::accent));
            g.fillRoundedRectangle (checkBox.toFloat().reduced (3.0f), 2.0f);
            g.setColour (juce::Colour (P::bg0));
            juce::Path tick;
            const auto cb = checkBox.toFloat();
            tick.startNewSubPath (cb.getX() + 4.5f, cb.getCentreY() + 0.5f);
            tick.lineTo (cb.getCentreX() - 1.0f, cb.getBottom() - 5.0f);
            tick.lineTo (cb.getRight() - 4.0f, cb.getY() + 5.0f);
            g.strokePath (tick, juce::PathStrokeType (1.8f));
        }
        area.removeFromLeft (4);

        // portada
        auto art = area.removeFromLeft (height - 10);
        g.setColour (juce::Colour (P::control));
        g.fillRoundedRectangle (art.toFloat(), 4.0f);
        auto cover = library.getArtwork (catIdx);
        if (cover.isValid())
        {
            g.saveState();
            juce::Path clip; clip.addRoundedRectangle (art.toFloat(), 4.0f);
            g.reduceClipRegion (clip);
            g.drawImage (cover, art.toFloat(),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
            g.restoreState();
        }
        else
            drawNoteGlyph (g, art);

        // triangulo de reproduccion sobre la portada
        if (isPlaying)
        {
            auto ac = art.toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (ac, 4.0f);
            g.setColour (juce::Colour (0xff62d96b));
            juce::Path tri;
            const auto cc = ac.getCentre();
            tri.addTriangle (cc.x - 6, cc.y - 8, cc.x - 6, cc.y + 8, cc.x + 8, cc.y);
            g.fillPath (tri);
        }

        area.removeFromLeft (12);

        auto durBox   = area.removeFromRight (54);
        auto stateBox = area.removeFromRight (70);
        auto modeBox  = area.removeFromRight (54);
        auto albumBox = area.removeFromRight (juce::jmin (170, area.getWidth() / 3));

        g.setColour (juce::Colour (isPlaying ? P::accent : P::textHi));
        g.setFont (juce::FontOptions (14.0f, isPlaying ? juce::Font::bold : juce::Font::plain));
        g.drawText (it.displayTitle(), area.removeFromTop (height / 2 - 3),
                    juce::Justification::bottomLeft, true);

        g.setColour (juce::Colour (P::textMid));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (it.artist.isNotEmpty() ? it.artist : "Artista desconocido",
                    area, juce::Justification::topLeft, true);

        g.setColour (juce::Colour (P::textDim));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (it.album, albumBox, juce::Justification::centredLeft, true);
        g.drawText (it.lengthString(), durBox, juce::Justification::centredRight, true);

        // estado de analisis (si el tema esta en el motor del PlaylistManager)
        if (const auto* t = trackForFile (it.file))
        {
            juce::String label;
            juce::Colour col (P::textDim);
            bool stemBadge = false;
            switch (t->state)
            {
                case Track::State::Pending:    label = "en cola";   col = juce::Colour (P::textDim); break;
                case Track::State::Decoding:
                case Track::State::Analyzing:  label = "analizando"; col = juce::Colour (P::accent2); break;
                case Track::State::Error:      label = "error";      col = juce::Colour (0xffe05050); break;
                default:
                    if (t->show.valid)
                    {
                        if (! t->analysis.stems.empty()) { stemBadge = true; }
                        else { label = "listo"; col = juce::Colour (P::accent); }
                    }
                    break;
            }
            if (stemBadge)
            {
                // Insignia clara: este tema fue entrenado separando STEMS con IA.
                const juce::Colour sc (0xff37c8c8);
                auto bb = stateBox.toFloat().withSizeKeepingCentre (
                              juce::jmin (62.0f, (float) stateBox.getWidth()), 18.0f);
                g.setColour (sc.withAlpha (0.18f));
                g.fillRoundedRectangle (bb, 4.0f);
                g.setColour (sc);
                g.drawRoundedRectangle (bb, 4.0f, 1.0f);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText ("STEMS IA", bb, juce::Justification::centred, false);
            }
            else if (label.isNotEmpty())
            {
                g.setColour (col);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (label, stateBox, juce::Justification::centredRight, false);
            }
        }

        // insignia de modo de coreografia: IA (automatica) o Manual (piano-roll).
        if (const int pi = playlist.indexForFile (it.file); pi >= 0)
        {
            const bool manual = playlist.isManualMode (pi) && playlist.hasManualShow (pi);
            const juce::String badge = manual ? "Manual" : "IA";
            const juce::Colour bc (manual ? juce::Colour (0xff9a6ad0) : juce::Colour (P::accent2));
            auto bb = modeBox.toFloat().withSizeKeepingCentre (
                          juce::jmin (50.0f, (float) modeBox.getWidth()), 18.0f);
            g.setColour (bc.withAlpha (0.16f));
            g.fillRoundedRectangle (bb, 4.0f);
            g.setColour (bc);
            g.drawRoundedRectangle (bb, 4.0f, 1.0f);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (badge, bb, juce::Justification::centred, false);
        }
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        playFromRow (row);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showTrackMenu (row);
            return;
        }

        // Clic en la zona de la casilla (izquierda) = alterna la marca.
        if (e.x < 34)
        {
            if (auto* it = itemAtRow (row))
            {
                const auto path = it->file.getFullPathName();
                if (checkedPaths.count (path) > 0) checkedPaths.erase (path);
                else                               checkedPaths.insert (path);
                trackList.repaintRow (row);
                updateButtons();
            }
            return;
        }

        updateButtons();
    }

    void selectedRowsChanged (int) override { updateButtons(); }

    //==============================================================================
    // ListBoxModel de la barra lateral (playlists).

    struct PlaylistModel : public juce::ListBoxModel
    {
        LibraryPanel* owner = nullptr;

        int getNumRows() override { return 1 + (owner ? owner->library.numPlaylists() : 0); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (owner == nullptr) return;
            const bool isAll = (row == 0);
            if (selected) g.fillAll (juce::Colour (P::accent).withAlpha (0.16f));

            juce::String label = isAll ? "Biblioteca"
                : (row - 1 < owner->library.numPlaylists()
                       ? owner->library.playlists()[(size_t) (row - 1)].name : juce::String());

            auto rr = juce::Rectangle<int> (0, 0, width, height);
            auto ic = rr.removeFromLeft (28).toFloat();
            g.setColour (juce::Colour (selected ? P::accent : P::textDim));
            if (isAll)
                g.fillRoundedRectangle (ic.getCentreX() - 6, ic.getCentreY() - 6, 12, 12, 2.0f);
            else
                for (int i = 0; i < 3; ++i)
                    g.fillRect (ic.getCentreX() - 6, ic.getCentreY() - 5 + i * 4.0f, 12.0f, 1.6f);

            g.setColour (juce::Colour (isAll ? P::textHi : P::textMid));
            g.setFont (juce::FontOptions (13.0f, isAll ? juce::Font::bold : juce::Font::plain));
            g.drawText (label, rr.withTrimmedRight (8), juce::Justification::centredLeft, true);
        }

        void selectedRowsChanged (int) override { if (owner) owner->onSidebarChanged(); }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (owner && row > 0) owner->renamePlaylistDialog (row - 1);
        }
    };

    void onSidebarChanged()
    {
        rebuildVisible();
        trackList.deselectAllRows();
        trackList.updateContent();
        updateButtons();
        repaint();
    }

    int selectedPlaylistIndex() const
    {
        const int r = sidebar.getSelectedRow();
        return r <= 0 ? -1 : r - 1;   // -1 = toda la biblioteca
    }

    // Rutas de todos los temas de la lista seleccionada (o de toda la biblioteca).
    juce::StringArray currentPlaylistPaths() const
    {
        juce::StringArray paths;
        const int idx = selectedPlaylistIndex();
        if (idx >= 0)
        {
            for (int i : library.itemsForPlaylist (idx))
                paths.add (library.items()[(size_t) i].file.getFullPathName());
        }
        else
        {
            for (const auto& it : library.items())
                paths.add (it.file.getFullPathName());
        }
        return paths;
    }

    juce::String currentPlaylistName() const
    {
        const int idx = selectedPlaylistIndex();
        if (idx >= 0 && idx < library.numPlaylists())
            return library.playlists()[(size_t) idx].name;
        return "Biblioteca";
    }

    void updateButtons()
    {
        const bool inList = selectedPlaylistIndex() >= 0;
        const bool hasSel = trackList.getNumSelectedRows() > 0;
        addTracksButton.setVisible (inList);
        removeButton.setButtonText (inList ? "Quitar de lista" : "Quitar");
        removeButton.setEnabled (hasSel);
        playButton.setEnabled (hasSel);
        trainButton.setEnabled (hasSel || ! checkedPaths.empty());
        addToListButton.setEnabled (hasSel && library.numPlaylists() > 0);
        choreosButton.setEnabled (hasSel);
        if (listHeader != nullptr) listHeader->repaint();
    }

    //==============================================================================
    void rebuildVisible()
    {
        const auto base = library.itemsForPlaylist (selectedPlaylistIndex());
        visible = library.filterIndices (base, search.getText());
        if (listHeader != nullptr) listHeader->repaint();
    }

    const MusicLibrary::MediaItem* itemAtRow (int row) const
    {
        if (row < 0 || row >= (int) visible.size()) return nullptr;
        const int catIdx = visible[(size_t) row];
        const auto& items = library.items();
        if (catIdx < 0 || catIdx >= (int) items.size()) return nullptr;
        return &items[(size_t) catIdx];
    }

    juce::File selectedFile() const
    {
        if (auto* it = itemAtRow (trackList.getSelectedRow()))
            return it->file;
        return {};
    }

    juce::Array<juce::File> selectedFiles() const
    {
        juce::Array<juce::File> files;
        auto sel = trackList.getSelectedRows();
        for (int i = 0; i < sel.size(); ++i)
            if (auto* it = itemAtRow (sel[i]))
                files.add (it->file);
        return files;
    }

    const Track* trackForFile (const juce::File& f) const
    {
        for (int i = 0; i < playlist.size(); ++i)
            if (playlist.getTrack (i).file == f)
                return &playlist.getTrack (i);
        return nullptr;
    }

    void playSelected()
    {
        int row = trackList.getSelectedRow();
        if (row < 0 && ! visible.empty())
            row = 0;
        playFromRow (row);
    }

    // Arranca la cola en el orden VISIBLE (arriba->abajo) desde la fila dada.
    void playFromRow (int row)
    {
        if (row < 0 || row >= (int) visible.size())
            return;
        if (onPlayQueue)
            onPlayQueue (currentVisibleFiles(), row);
    }

    juce::Array<juce::File> currentVisibleFiles() const
    {
        juce::Array<juce::File> files;
        const auto& items = library.items();
        for (int catIdx : visible)
            if (catIdx >= 0 && catIdx < (int) items.size())
                files.add (items[(size_t) catIdx].file);
        return files;
    }

    /** Archivos VISIBLES con la casilla marcada (respeta lista y filtro actuales). */
    juce::Array<juce::File> checkedVisibleFiles() const
    {
        juce::Array<juce::File> files;
        const auto& items = library.items();
        for (int catIdx : visible)
            if (catIdx >= 0 && catIdx < (int) items.size())
            {
                const auto& f = items[(size_t) catIdx].file;
                if (checkedPaths.count (f.getFullPathName()) > 0)
                    files.add (f);
            }
        return files;
    }

    /** Estado de la casilla "Seleccionar todo": 0 = ninguno, 1 = todos, 2 = parcial.
        Solo cuenta los temas VISIBLES (lista + filtro actuales). */
    int selectAllState() const
    {
        int total = 0, marked = 0;
        const auto& items = library.items();
        for (int catIdx : visible)
            if (catIdx >= 0 && catIdx < (int) items.size())
            {
                ++total;
                if (checkedPaths.count (items[(size_t) catIdx].file.getFullPathName()) > 0)
                    ++marked;
            }
        if (total == 0 || marked == 0) return 0;
        return (marked == total) ? 1 : 2;
    }

    int visibleCheckedCount() const
    {
        int marked = 0;
        const auto& items = library.items();
        for (int catIdx : visible)
            if (catIdx >= 0 && catIdx < (int) items.size()
                && checkedPaths.count (items[(size_t) catIdx].file.getFullPathName()) > 0)
                ++marked;
        return marked;
    }

    /** Marca/desmarca todos los temas VISIBLES (si ya estan todos marcados, los limpia). */
    void toggleSelectAll()
    {
        const bool allChecked = (selectAllState() == 1);
        const auto& items = library.items();
        for (int catIdx : visible)
            if (catIdx >= 0 && catIdx < (int) items.size())
            {
                const auto path = items[(size_t) catIdx].file.getFullPathName();
                if (allChecked) checkedPaths.erase (path);
                else            checkedPaths.insert (path);
            }
        trackList.repaint();
        updateButtons();
    }

    /** Entrena los temas marcados; si no hay ninguno marcado, usa la seleccion actual. */
    void trainAction()
    {
        auto files = checkedVisibleFiles();
        if (files.isEmpty())
            files = selectedFiles();

        if (files.isEmpty())
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Entrenar", "Marca la casilla de los temas que quieres entrenar "
                            "(o usa \"Seleccionar todo\" arriba de la lista).");
            return;
        }
        if (onTrainFiles) onTrainFiles (files);
    }

    void removeSelected()
    {
        const int pl = selectedPlaylistIndex();
        auto files = selectedFiles();
        for (const auto& f : files)
        {
            if (pl >= 0) library.removeFromPlaylist (pl, f);
            else         { const int i = library.indexForPath (f.getFullPathName()); if (i >= 0) library.removeItem (i); }
        }
    }

    //==============================================================================
    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Elige una carpeta de musica", juce::File());
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f.isDirectory())
                    library.addFolder (f);
            });
    }

    void chooseFiles()
    {
        chooser = std::make_unique<juce::FileChooser> ("Elige archivos de musica",
                                                       juce::File(), library.getSupportedWildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                const auto results = fc.getResults();
                if (! results.isEmpty())
                    library.addFiles (results);
            });
    }

    void createPlaylist()
    {
        askName ("Nueva lista", "Mi lista", [this] (const juce::String& name)
        {
            const int idx = library.createPlaylist (name);
            sidebar.selectRow (idx + 1);
        });
    }

    void deleteSelectedPlaylist()
    {
        const int idx = selectedPlaylistIndex();
        if (idx < 0) return;
        const auto name = library.playlists()[(size_t) idx].name;
        juce::NativeMessageBox::showYesNoBox (juce::MessageBoxIconType::QuestionIcon,
            "Borrar lista", "Borrar la lista \"" + name + "\"? (no borra los temas de la biblioteca)",
            nullptr, juce::ModalCallbackFunction::create ([this, idx] (int r)
            {
                if (r == 1) { library.removePlaylist (idx); sidebar.selectRow (0); }
            }));
    }

    void renamePlaylistDialog (int playlistIndex)
    {
        if (playlistIndex < 0 || playlistIndex >= library.numPlaylists()) return;
        askName ("Renombrar lista", library.playlists()[(size_t) playlistIndex].name,
            [this, playlistIndex] (const juce::String& name)
            {
                library.renamePlaylist (playlistIndex, name);
            });
    }

    void showAddToListMenu()
    {
        auto files = selectedFiles();
        if (files.isEmpty()) return;

        juce::PopupMenu m;
        for (int i = 0; i < library.numPlaylists(); ++i)
            m.addItem (1000 + i, library.playlists()[(size_t) i].name);
        m.addSeparator();
        m.addItem (900, "Nueva lista...");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addToListButton),
            [this, files] (int choice)
            {
                if (choice >= 1000)
                {
                    const int pl = choice - 1000;
                    for (const auto& f : files) library.addToPlaylist (pl, f);
                }
                else if (choice == 900)
                {
                    askName ("Nueva lista", "Mi lista", [this, files] (const juce::String& name)
                    {
                        const int idx = library.createPlaylist (name);
                        for (const auto& f : files) library.addToPlaylist (idx, f);
                        sidebar.selectRow (idx + 1);
                    });
                }
            });
    }

    void showTrackMenu (int row)
    {
        auto* it = itemAtRow (row);
        if (it == nullptr) return;
        if (! trackList.isRowSelected (row))
            trackList.selectRow (row);
        const auto file = it->file;
        const int selPl = selectedPlaylistIndex();
        const int pi    = playlist.indexForFile (file);

        juce::PopupMenu m;
        m.addItem (1, "Reproducir");
        m.addSeparator();

        // Modo de coreografia: IA (automatica) o Manual (piano-roll).
        if (pi >= 0)
        {
            const bool manual    = playlist.isManualMode (pi);
            const bool hasManual = playlist.hasManualShow (pi);

            juce::PopupMenu cm;
            cm.addItem (20, "Auto (IA)", true, ! manual);
            cm.addItem (21, "Manual (editada)", hasManual, manual && hasManual);
            cm.addSeparator();
            cm.addItem (22, "Crear manual desde IA (hornear)");
            cm.addItem (23, "Crear manual en blanco");
            cm.addItem (25, "Editar (piano roll)...");
            cm.addItem (24, "Descartar manual", hasManual);
            m.addSubMenu ("Coreografia", cm);
            m.addSeparator();
        }

        juce::PopupMenu addTo;
        for (int i = 0; i < library.numPlaylists(); ++i)
            addTo.addItem (1000 + i, library.playlists()[(size_t) i].name);
        addTo.addSeparator();
        addTo.addItem (900, "Nueva lista...");
        m.addSubMenu ("Anadir a la lista", addTo);
        if (selPl >= 0) m.addItem (3, "Quitar de esta lista");
        m.addSeparator();
        m.addItem (4, "Quitar de la biblioteca");
        m.addSeparator();
        m.addItem (6, "Propiedades...");

        m.showMenuAsync (juce::PopupMenu::Options(),
            [this, file, selPl, row, pi] (int choice)
            {
                if (choice == 1) { playFromRow (row); }
                else if (choice == 6) { if (onShowProperties) onShowProperties (file); }
                else if (choice == 20 && pi >= 0) playlist.setChoreoMode (pi, Track::ChoreoMode::Auto);
                else if (choice == 21 && pi >= 0) playlist.setChoreoMode (pi, Track::ChoreoMode::Manual);
                else if (choice == 22 && pi >= 0) playlist.bakeManualFromAuto (pi);
                else if (choice == 23 && pi >= 0) playlist.createBlankManual (pi);
                else if (choice == 25 && pi >= 0) { if (onEditManual) onEditManual (pi); }
                else if (choice == 24 && pi >= 0) playlist.discardManual (pi);
                else if (choice == 3 && selPl >= 0) library.removeFromPlaylist (selPl, file);
                else if (choice == 4) { const int i = library.indexForPath (file.getFullPathName()); if (i >= 0) library.removeItem (i); }
                else if (choice == 900) askName ("Nueva lista", "Mi lista", [this, file] (const juce::String& name)
                                        { const int idx = library.createPlaylist (name); library.addToPlaylist (idx, file); });
                else if (choice >= 1000) library.addToPlaylist (choice - 1000, file);
            });
    }

    //==============================================================================
    // Dialogo para elegir temas de la biblioteca y anadirlos a la lista actual.

    struct SelectDialog : public juce::Component, private juce::ListBoxModel
    {
        MusicLibrary& lib;
        std::vector<int> shown;
        juce::TextEditor sBox;
        juce::ListBox    box;
        juce::TextButton addBtn { "Anadir" }, cancelBtn { "Cancelar" };
        std::function<void (const juce::Array<juce::File>&)> onAdd;

        explicit SelectDialog (MusicLibrary& l) : lib (l)
        {
            sBox.setTextToShowWhenEmpty ("Buscar...", juce::Colour (P::textDim));
            sBox.onTextChange = [this] { rebuild(); box.updateContent(); };
            addAndMakeVisible (sBox);
            box.setModel (this);
            box.setRowHeight (24);
            box.setMultipleSelectionEnabled (true);
            box.setColour (juce::ListBox::backgroundColourId, juce::Colour (P::bg0));
            addAndMakeVisible (box);
            addAndMakeVisible (addBtn);
            addAndMakeVisible (cancelBtn);
            addBtn.onClick = [this]
            {
                juce::Array<juce::File> files;
                auto sel = box.getSelectedRows();
                for (int i = 0; i < sel.size(); ++i)
                {
                    const int r = sel[i];
                    if (r >= 0 && r < (int) shown.size())
                        files.add (lib.items()[(size_t) shown[(size_t) r]].file);
                }
                if (onAdd) onAdd (files);
                close();
            };
            cancelBtn.onClick = [this] { close(); };
            rebuild();
            setSize (460, 420);
        }

        void rebuild()
        {
            std::vector<int> all;
            for (int i = 0; i < lib.numItems(); ++i) all.push_back (i);
            shown = lib.filterIndices (all, sBox.getText());
        }

        void close() { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); }

        int getNumRows() override { return (int) shown.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            if (row < 0 || row >= (int) shown.size()) return;
            const auto& it = lib.items()[(size_t) shown[(size_t) row]];
            if (sel) g.fillAll (juce::Colour (P::accent).withAlpha (0.18f));
            g.setColour (juce::Colour (P::textHi));
            g.setFont (juce::FontOptions (13.0f));
            const auto label = it.displayTitle() + (it.artist.isNotEmpty() ? "  -  " + it.artist : juce::String());
            g.drawText (label, juce::Rectangle<int> (10, 0, w - 14, h), juce::Justification::centredLeft, true);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (P::bg1)); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            sBox.setBounds (r.removeFromTop (28));
            r.removeFromTop (8);
            auto btns = r.removeFromBottom (32);
            cancelBtn.setBounds (btns.removeFromRight (100));
            btns.removeFromRight (8);
            addBtn.setBounds (btns.removeFromRight (100));
            r.removeFromBottom (8);
            box.setBounds (r);
        }
    };

    void openAddTracksDialog()
    {
        const int pl = selectedPlaylistIndex();
        if (pl < 0) return;
        if (library.numItems() == 0)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Biblioteca vacia", "Anade musica con \"+ Carpeta\" o \"+ Archivos\" antes de crear listas.");
            return;
        }

        auto dlg = std::make_unique<SelectDialog> (library);
        dlg->onAdd = [this, pl] (const juce::Array<juce::File>& files)
        {
            for (const auto& f : files) library.addToPlaylist (pl, f);
        };

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (dlg.release());
        opts.dialogTitle = "Anadir pistas a la lista";
        opts.dialogBackgroundColour = juce::Colour (P::bg1);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = false;
        opts.launchAsync();
    }

    template <typename Callback>
    void askName (const juce::String& title_, const juce::String& initial, Callback cb)
    {
        auto* aw = new juce::AlertWindow (title_, "Nombre:", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", initial);
        aw->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [aw, cb] (int result)
            {
                if (result == 1)
                {
                    const auto name = aw->getTextEditorContents ("name").trim();
                    if (name.isNotEmpty()) cb (name);
                }
                delete aw;
            }), false);
    }

    //==============================================================================
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refreshFromLibrary();
    }

    void timerCallback() override
    {
        const juce::String np = getNowPlayingFile ? getNowPlayingFile().getFullPathName() : juce::String();
        const bool dirty = (np != nowPlayingPath);
        nowPlayingPath = np;

        const juce::String s = library.isScanning()
            ? "Escaneando... (" + juce::String (library.pendingScanCount()) + ")"
            : juce::String (library.numItems()) + " temas";

        // Mensaje temporal (flash) tras una accion; prioritario sobre el texto normal.
        const auto now = juce::Time::getMillisecondCounter();
        if (now < flashUntilMs)
        {
            if (flashMsg != status.getText())
            {
                status.setColour (juce::Label::textColourId, juce::Colour (P::accent));
                status.setText (flashMsg, juce::dontSendNotification);
            }
        }
        else
        {
            if (status.findColour (juce::Label::textColourId) != juce::Colour (P::textDim))
                status.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
            if (s != status.getText())
                status.setText (s, juce::dontSendNotification);
        }

        if (dirty)
        {
            trackList.repaint();
            repaint();
        }
        else if (anyTrackBusy())
        {
            trackList.repaint();   // refresca el estado "analizando"
        }
    }

    /** Muestra un mensaje temporal en la barra de estado (unos segundos). */
    void flashStatus (const juce::String& msg, int ms = 4000)
    {
        flashMsg     = msg;
        flashUntilMs = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        using P = LuxLookAndFeel::Palette;
        status.setColour (juce::Label::textColourId, juce::Colour (P::accent));
        status.setText (msg, juce::dontSendNotification);
    }

    bool anyTrackBusy() const
    {
        for (int i = 0; i < playlist.size(); ++i)
        {
            const auto st = playlist.getTrack (i).state;
            if (st == Track::State::Pending || st == Track::State::Decoding || st == Track::State::Analyzing)
                return true;
        }
        return false;
    }

    //==============================================================================
    // Cabecera de la lista: casilla "Seleccionar todo" (solo temas visibles).
    struct ListHeader : public juce::Component
    {
        LibraryPanel& owner;
        explicit ListHeader (LibraryPanel& o) : owner (o) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (P::bg1));

            auto area = getLocalBounds().reduced (8, 0);
            auto checkBox = area.removeFromLeft (24).withSizeKeepingCentre (18, 18);

            const int state = owner.selectAllState();   // 0 ninguno, 1 todos, 2 parcial
            g.setColour (juce::Colour (state != 0 ? P::accent : P::line));
            g.drawRoundedRectangle (checkBox.toFloat(), 3.0f, 1.4f);

            if (state == 1)   // todos -> tick
            {
                g.setColour (juce::Colour (P::accent));
                g.fillRoundedRectangle (checkBox.toFloat().reduced (3.0f), 2.0f);
                g.setColour (juce::Colour (P::bg0));
                juce::Path tick;
                const auto cb = checkBox.toFloat();
                tick.startNewSubPath (cb.getX() + 4.5f, cb.getCentreY() + 0.5f);
                tick.lineTo (cb.getCentreX() - 1.0f, cb.getBottom() - 5.0f);
                tick.lineTo (cb.getRight() - 4.0f, cb.getY() + 5.0f);
                g.strokePath (tick, juce::PathStrokeType (1.8f));
            }
            else if (state == 2)   // parcial -> guion
            {
                g.setColour (juce::Colour (P::accent));
                auto cb = checkBox.toFloat().reduced (5.0f);
                g.fillRoundedRectangle (cb.getX(), cb.getCentreY() - 1.5f, cb.getWidth(), 3.0f, 1.5f);
            }

            area.removeFromLeft (6);
            const int marked = owner.visibleCheckedCount();
            juce::String label = "Seleccionar todo";
            if (marked > 0) label << "   (" << marked << " marcados)";
            g.setColour (juce::Colour (P::textMid));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (label, area, juce::Justification::centredLeft, true);

            g.setColour (juce::Colour (P::line));
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
        }

        void mouseUp (const juce::MouseEvent&) override { owner.toggleSelectAll(); }
        void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    };

    //==============================================================================
    MusicLibrary&    library;
    PlaylistManager& playlist;
    std::vector<int> visible;
    std::set<juce::String> checkedPaths;   // temas con la casilla marcada (para "Entrenar")
    juce::String     nowPlayingPath;
    juce::Rectangle<int> bannerBounds;

    juce::String     flashMsg;          // mensaje temporal de estado
    juce::uint32     flashUntilMs = 0;  // hasta cuando mostrarlo (ms)

    juce::Label       title, status, styleLabel, moveLabel;
    juce::TextButton  addFolderButton, addFilesButton, newListButton, delListButton;
    juce::TextButton  playButton, addToListButton, addTracksButton, removeButton;
    juce::TextButton  trainButton;
    juce::TextButton  regenButton;
    juce::TextButton  choreosButton;
    juce::ToggleButton shuffleToggle, repeatToggle;
    juce::ComboBox    styleCombo;
    juce::ComboBox    moveCombo;
    juce::TextEditor  search;
    juce::ListBox     sidebar;
    juce::ListBox     trackList;
    ListHeader*       listHeader = nullptr;   // propiedad del trackList (setHeaderComponent)
    PlaylistModel     playlistModel;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};
