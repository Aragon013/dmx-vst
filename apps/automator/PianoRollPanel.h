#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/LuxLookAndFeel.h"
#include "PlaylistManager.h"
#include "ManualShowEditor.h"

/**
    Pestana "Piano roll" del Automator.

    Editor de coreografias MANUALES integrado en la app (no en una ventana aparte).
    Arriba: un selector con todas las canciones de la lista; al elegir una, si no
    tiene coreografia manual se pregunta como crearla (desde la IA u en blanco) y
    se carga su piano roll debajo.

    Las ediciones se aplican sobre la coreografia manual del tema (vista previa en
    vivo en el escenario), pero NO se guardan en disco hasta pulsar "Guardar Piano
    roll". Si hay cambios sin guardar y se cambia de cancion, se avisa: continuar
    descarta los cambios (se restaura el estado guardado).
*/
class PianoRollPanel : public juce::Component
{
public:
    using P = LuxLookAndFeel::Palette;

    // Refresca el escenario/preview tras una edicion (vista previa en vivo).
    std::function<void()> onShowEdited;
    // Posicion del reproductor en segundos para el playhead (arg = indice de playlist; <0 si no aplica).
    std::function<double (int)> getPlayheadSecondsFor;
    std::function<bool   (int)> isPlayingFor;
    // Reproduce / pausa el tema (indice de playlist) - barra espaciadora del editor.
    std::function<void (int)> onTogglePlay;
    // Mueve el reproductor del tema (indice de playlist) a una posicion en segundos.
    std::function<void (int, double)> onSeekSeconds;
    // Abre una ventana aparte con el escenario.
    std::function<void()> onOpenStageWindow;

    explicit PianoRollPanel (PlaylistManager& pm) : playlist (pm)
    {
        title.setText ("Piano roll", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        songLabel.setText ("Cancion", juce::dontSendNotification);
        songLabel.setFont (juce::FontOptions (12.0f));
        songLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (songLabel);

        songCombo.setTextWhenNothingSelected ("(elige una cancion)");
        songCombo.onChange = [this] { onSongComboChanged(); };
        addAndMakeVisible (songCombo);

        saveButton.setButtonText ("Guardar Piano roll");
        saveButton.setColour (juce::TextButton::buttonColourId, juce::Colour (P::accent));
        saveButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::bg0));
        saveButton.onClick = [this] { save(); };
        addAndMakeVisible (saveButton);

        stageWindowButton.setButtonText ("Escenario aparte");
        stageWindowButton.setTooltip ("Abre una ventana con el escenario para verlo mientras editas el piano roll.");
        stageWindowButton.onClick = [this] { if (onOpenStageWindow) onOpenStageWindow(); };
        addAndMakeVisible (stageWindowButton);

        statusLabel.setFont (juce::FontOptions (12.0f));
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        statusLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (statusLabel);

        placeholder.setText ("Elige una cancion arriba para editar su coreografia manual.",
                             juce::dontSendNotification);
        placeholder.setFont (juce::FontOptions (14.0f));
        placeholder.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        placeholder.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (placeholder);

        refreshSongList();
        updateStatus();
    }

    //==============================================================================
    /** Reconstruye el combo de canciones desde la lista actual. */
    void refreshSongList()
    {
        songCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < playlist.size(); ++i)
        {
            juce::String name = playlist.getTrack (i).displayName;
            if (playlist.hasManualShow (i))
                name = juce::String::fromUTF8 ("\xE2\x97\x8F ") + name;   // punto: tiene manual
            songCombo.addItem (name, i + 1);
        }

        if (currentIndex >= 0 && currentIndex < playlist.size())
            songCombo.setSelectedId (currentIndex + 1, juce::dontSendNotification);
        else if (currentIndex >= playlist.size())
        {
            currentIndex = -1;
            rebuildEditor();
            updateStatus();
        }
    }

    /** Entrada externa (p.ej. desde el menu contextual): selecciona una cancion
        gestionando el aviso de cambios sin guardar y la pregunta de creacion. */
    void selectSong (int playlistIndex)
    {
        if (playlistIndex < 0 || playlistIndex >= playlist.size())
            return;
        if (playlistIndex == currentIndex)
            return;

        if (dirty && currentIndex >= 0)
        {
            confirmDiscardThen (playlistIndex);
            return;
        }
        proceedSelect (playlistIndex);
    }

    int getCurrentIndex() const noexcept { return currentIndex; }

    //==============================================================================
    void resized() override
    {
        auto area = getLocalBounds().reduced (10);

        auto top = area.removeFromTop (30);
        title.setBounds (top.removeFromLeft (140));
        statusLabel.setBounds (top.removeFromRight (220));

        auto bar = area.removeFromTop (34);
        songLabel.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 24));
        bar.removeFromLeft (4);
        songCombo.setBounds (bar.removeFromLeft (320).withSizeKeepingCentre (320, 28));
        saveButton.setBounds (bar.removeFromRight (160).withSizeKeepingCentre (160, 28));
        bar.removeFromRight (8);
        stageWindowButton.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 28));

        area.removeFromTop (6);
        editorArea = area;
        placeholder.setBounds (area);
        if (editor != nullptr)
            editor->setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (P::bg1));
    }

private:
    //==============================================================================
    void onSongComboChanged()
    {
        const int id = songCombo.getSelectedId();
        if (id <= 0)
            return;
        const int idx = id - 1;
        if (idx == currentIndex)
            return;

        if (dirty && currentIndex >= 0)
        {
            confirmDiscardThen (idx);
            return;
        }
        proceedSelect (idx);
    }

    void confirmDiscardThen (int idx)
    {
        const auto name = (currentIndex >= 0 && currentIndex < playlist.size())
                            ? playlist.getTrack (currentIndex).displayName : juce::String();
        juce::NativeMessageBox::showYesNoBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Cambios sin guardar",
            "El piano roll de:\n\n" + name + "\n\ntiene cambios sin guardar.\n\n"
            "Si cambias de cancion se perderan.\n\n" "Quieres cambiar igualmente?",
            this,
            juce::ModalCallbackFunction::create ([this, idx] (int result)
            {
                if (result == 1)        // Si -> descartar y cambiar
                {
                    discardCurrent();
                    proceedSelect (idx);
                }
                else                    // No -> seguir en la cancion actual
                {
                    syncCombo();
                }
            }));
    }

    void proceedSelect (int idx)
    {
        if (! playlist.hasManualShow (idx))
        {
            const auto name = playlist.getTrack (idx).displayName;
            juce::NativeMessageBox::showYesNoCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Crear coreografia manual",
                "El tema:\n\n" + name + "\n\nno tiene coreografia manual todavia.\n\n"
                "Si = partir de la coreografia IA (hornear)\n"
                "No = empezar en blanco\n"
                "Cancelar = no hacer nada",
                this,
                juce::ModalCallbackFunction::create ([this, idx] (int result)
                {
                    if (result == 0)              // Cancelar
                    {
                        syncCombo();
                        return;
                    }
                    if (result == 1)              // Si -> desde IA
                        playlist.bakeManualFromAuto (idx);
                    else                           // No -> en blanco
                        playlist.createBlankManual (idx);

                    doLoad (idx);
                }));
            return;
        }

        playlist.setChoreoMode (idx, Track::ChoreoMode::Manual);
        doLoad (idx);
    }

    void doLoad (int idx)
    {
        currentIndex = idx;
        if (auto* ref = playlist.manualShowRef (idx))
            snapshot = *ref;          // estado "guardado" de partida
        dirty = false;
        rebuildEditor();
        syncCombo();
        refreshSongList();            // refresca el punto de "tiene manual"
        updateStatus();
        if (onShowEdited)
            onShowEdited();
    }

    void rebuildEditor()
    {
        editor.reset();
        if (currentIndex < 0 || currentIndex >= playlist.size())
        {
            placeholder.setVisible (true);
            return;
        }
        auto* ref = playlist.manualShowRef (currentIndex);
        if (ref == nullptr)
        {
            placeholder.setVisible (true);
            return;
        }

        placeholder.setVisible (false);
        const auto name = playlist.getTrack (currentIndex).displayName;
        editor = std::make_unique<ManualShowEditor> (*ref, name);

        editor->onChanged = [this]
        {
            dirty = true;
            updateStatus();
            if (onShowEdited)
                onShowEdited();
        };
        const int idx = currentIndex;
        editor->getPlayheadSeconds = [this, idx]() -> double
        {
            return getPlayheadSecondsFor ? getPlayheadSecondsFor (idx) : -1.0;
        };
        editor->isPlaying = [this, idx]() -> bool
        {
            return isPlayingFor ? isPlayingFor (idx) : false;
        };
        editor->onTogglePlay = [this, idx]
        {
            if (onTogglePlay) onTogglePlay (idx);
        };
        editor->onSeekSeconds = [this, idx] (double secs)
        {
            if (onSeekSeconds) onSeekSeconds (idx, secs);
        };

        addAndMakeVisible (*editor);
        if (! editorArea.isEmpty())
            editor->setBounds (editorArea);
    }

    void save()
    {
        if (currentIndex < 0 || currentIndex >= playlist.size())
            return;
        playlist.manualShowEdited (currentIndex);     // persiste la sesion
        if (auto* ref = playlist.manualShowRef (currentIndex))
            snapshot = *ref;                          // nuevo punto guardado
        dirty = false;
        updateStatus();
    }

    void discardCurrent()
    {
        if (currentIndex < 0 || currentIndex >= playlist.size())
            return;
        if (auto* ref = playlist.manualShowRef (currentIndex))
            *ref = snapshot;          // revierte las ediciones en memoria
        dirty = false;
        if (onShowEdited)
            onShowEdited();
    }

    void syncCombo()
    {
        songCombo.setSelectedId (currentIndex >= 0 ? currentIndex + 1 : 0,
                                 juce::dontSendNotification);
    }

    void updateStatus()
    {
        if (currentIndex < 0)
        {
            statusLabel.setText ({}, juce::dontSendNotification);
            saveButton.setEnabled (false);
            return;
        }
        saveButton.setEnabled (dirty);
        statusLabel.setText (dirty ? "Cambios sin guardar" : "Guardado",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId,
                               juce::Colour (dirty ? P::accent : P::textDim));
    }

    //==============================================================================
    PlaylistManager& playlist;

    juce::Label      title, songLabel, statusLabel, placeholder;
    juce::ComboBox   songCombo;
    juce::TextButton saveButton;
    juce::TextButton stageWindowButton;

    std::unique_ptr<ManualShowEditor> editor;
    juce::Rectangle<int> editorArea;

    int     currentIndex = -1;
    bool    dirty = false;
    DmxShow snapshot;     // estado guardado de la cancion en edicion (para descartar)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
};
