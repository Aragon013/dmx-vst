#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/LuxLookAndFeel.h"
#include "PlaylistManager.h"

/**
    Ventana de Preferencias del Automator (Fase 4).

    De momento, dos ajustes:
      - Carpeta de musica por defecto (donde abre el selector "Anadir temas").
      - Autocargar el ultimo proyecto .lux al abrir la app.
*/
class PreferencesPanel : public juce::Component
{
public:
    using P = LuxLookAndFeel::Palette;

    explicit PreferencesPanel (PlaylistManager& pm) : playlist (pm)
    {
        title.setText ("Preferencias", juce::dontSendNotification);
        title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        folderLabel.setText ("Carpeta de musica por defecto", juce::dontSendNotification);
        folderLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (folderLabel);

        folderValue.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        folderValue.setColour (juce::Label::backgroundColourId, juce::Colour (P::bg0));
        folderValue.setJustificationType (juce::Justification::centredLeft);
        folderValue.setBorderSize (juce::BorderSize<int> (4, 8, 4, 8));
        addAndMakeVisible (folderValue);

        chooseButton.setButtonText ("Elegir...");
        chooseButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (chooseButton);

        clearButton.setButtonText ("Quitar");
        clearButton.onClick = [this]
        {
            playlist.setMusicFolder (juce::File());
            refresh();
        };
        addAndMakeVisible (clearButton);

        autoLoadToggle.setButtonText ("Autocargar el ultimo proyecto al abrir la app");
        autoLoadToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (P::textMid));
        autoLoadToggle.onClick = [this]
        {
            playlist.setAutoLoadLastProject (autoLoadToggle.getToggleState());
        };
        addAndMakeVisible (autoLoadToggle);

        closeButton.setButtonText ("Cerrar");
        closeButton.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };
        addAndMakeVisible (closeButton);

        refresh();
        setSize (520, 230);
    }

    void refresh()
    {
        const auto f = playlist.getMusicFolder();
        folderValue.setText (f.isDirectory() ? f.getFullPathName() : juce::String ("(carpeta del sistema)"),
                             juce::dontSendNotification);
        autoLoadToggle.setToggleState (playlist.getAutoLoadLastProject(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);

        folderLabel.setBounds (area.removeFromTop (20));
        auto row = area.removeFromTop (28);
        clearButton.setBounds (row.removeFromRight (70));
        row.removeFromRight (6);
        chooseButton.setBounds (row.removeFromRight (80));
        row.removeFromRight (8);
        folderValue.setBounds (row);

        area.removeFromTop (14);
        autoLoadToggle.setBounds (area.removeFromTop (24));

        auto bottom = area.removeFromBottom (30);
        closeButton.setBounds (bottom.removeFromRight (90));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (P::bg1));
    }

private:
    void chooseFolder()
    {
        auto start = playlist.getMusicFolder();
        if (! start.isDirectory())
            start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Carpeta de musica por defecto", start);
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (dir.isDirectory())
                {
                    playlist.setMusicFolder (dir);
                    refresh();
                }
            });
    }

    PlaylistManager& playlist;

    juce::Label   title, folderLabel, folderValue;
    juce::TextButton chooseButton, clearButton, closeButton;
    juce::ToggleButton autoLoadToggle;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesPanel)
};
