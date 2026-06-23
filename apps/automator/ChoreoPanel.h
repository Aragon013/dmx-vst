#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../source/FixtureModel.h"
#include "../../source/LuxLookAndFeel.h"
#include "ChoreographyEngine.h"
#include "PlaylistManager.h"

/**
    Pantalla "Coreografias": crea/edita coreografias propias atadas a una fixtura
    concreta (o a todas). Cada coreografia es un "look" = patron de movimiento +
    estilo de color + parametros. Luego, por cancion, se elige un conjunto de
    estas (boton "Coreografias..." en el Reproductor) y el Auto IA las reparte por
    energia, de los momentos calmos al climax.
*/
class ChoreoPanel : public juce::Component,
                    private juce::ListBoxModel
{
public:
    explicit ChoreoPanel (PlaylistManager& pm) : playlist (pm)
    {
        using P = LuxLookAndFeel::Palette;

        title.setText ("Coreografias", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        hint.setText ("Crea looks por fixtura (patron + color). Por cancion eliges cuales usar y el Auto IA los reparte por energia.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        hint.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (hint);

        list.setModel (this);
        list.setRowHeight (26);
        addAndMakeVisible (list);

        newButton.setButtonText ("+ Nueva");
        newButton.onClick = [this] { startNew(); };
        addAndMakeVisible (newButton);

        deleteButton.setButtonText ("Eliminar");
        deleteButton.onClick = [this] { deleteSelected(); };
        addAndMakeVisible (deleteButton);

        auto setupLabel = [this] (juce::Label& l, const juce::String& txt)
        {
            l.setText (txt, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, juce::Colour (LuxLookAndFeel::Palette::textMid));
            l.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (l);
        };

        setupLabel (nameLabel, "Nombre");
        addAndMakeVisible (nameEditor);
        nameEditor.setTextToShowWhenEmpty ("Mi coreografia", juce::Colour (P::textDim));

        setupLabel (targetLabel, "Fixtura");
        addAndMakeVisible (targetCombo);

        setupLabel (motionLabel, "Patron");
        motionCombo.addItemList (ChoreographyEngine::motionStyleNames(), 1);
        addAndMakeVisible (motionCombo);

        setupLabel (colorLabel, "Color");
        colorCombo.addItemList (ChoreographyEngine::colorStyleNames(), 1);
        addAndMakeVisible (colorCombo);

        setupLabel (colorBeatsLabel, "Compas de color");
        colorBeatsSlider.setSliderStyle (juce::Slider::IncDecButtons);
        colorBeatsSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
        colorBeatsSlider.setRange (0.25, 16.0, 0.25);
        addAndMakeVisible (colorBeatsSlider);

        setupLabel (moveSpeedLabel, "Velocidad");
        moveSpeedSlider.setSliderStyle (juce::Slider::IncDecButtons);
        moveSpeedSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
        moveSpeedSlider.setRange (0.25, 8.0, 0.25);
        addAndMakeVisible (moveSpeedSlider);

        saveButton.setButtonText ("Guardar coreografia");
        saveButton.onClick = [this] { commit(); };
        addAndMakeVisible (saveButton);

        refreshTargets();
        startNew();
        refreshList();
    }

    /** Refresca lista y combo de fixturas cuando cambia algo por fuera. */
    void refreshFromPlaylist()
    {
        refreshTargets();
        refreshList();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (LuxLookAndFeel::Palette::bg0));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        title.setBounds (r.removeFromTop (26));
        hint.setBounds (r.removeFromTop (20));
        r.removeFromTop (8);

        auto left = r.removeFromLeft (juce::jmin (300, r.getWidth() / 2));
        auto leftButtons = left.removeFromBottom (32);
        newButton.setBounds (leftButtons.removeFromLeft (110));
        leftButtons.removeFromLeft (8);
        deleteButton.setBounds (leftButtons.removeFromLeft (110));
        left.removeFromBottom (8);
        list.setBounds (left);

        r.removeFromLeft (16);
        auto ed = r;

        auto rowH = 30;
        auto labelW = 130;
        auto field = [&] (juce::Component& l, juce::Component& c, int h = 30)
        {
            auto row = ed.removeFromTop (h);
            l.setBounds (row.removeFromLeft (labelW));
            c.setBounds (row);
            ed.removeFromTop (8);
        };

        field (nameLabel, nameEditor);
        field (targetLabel, targetCombo);
        field (motionLabel, motionCombo);
        field (colorLabel, colorCombo);
        {
            auto row = ed.removeFromTop (rowH);
            colorBeatsLabel.setBounds (row.removeFromLeft (labelW));
            colorBeatsSlider.setBounds (row.removeFromLeft (160));
            ed.removeFromTop (8);
        }
        {
            auto row = ed.removeFromTop (rowH);
            moveSpeedLabel.setBounds (row.removeFromLeft (labelW));
            moveSpeedSlider.setBounds (row.removeFromLeft (160));
            ed.removeFromTop (8);
        }
        ed.removeFromTop (8);
        saveButton.setBounds (ed.removeFromTop (34).removeFromLeft (200));
    }

private:
    //==============================================================================
    // ListBoxModel.
    int getNumRows() override { return (int) playlist.getChoreoLibrary().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        using P = LuxLookAndFeel::Palette;
        const auto& lib = playlist.getChoreoLibrary();
        if (row < 0 || row >= (int) lib.size())
            return;

        if (selected)
            g.fillAll (juce::Colour (P::accent).withAlpha (0.18f));

        const auto& c = lib[(size_t) row];
        g.setColour (juce::Colour (selected ? P::textHi : P::textMid));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (c.name, 8, 0, w - 90, h, juce::Justification::centredLeft);

        const juce::String tgt = c.targetKey.isEmpty() ? "Todas" : targetShort (c.targetKey);
        g.setColour (juce::Colour (P::textDim));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (tgt, w - 86, 0, 80, h, juce::Justification::centredRight);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        loadRow (row);
    }

    //==============================================================================
    void refreshTargets()
    {
        targetCombo.clear (juce::dontSendNotification);
        targetKeys.clear();

        targetCombo.addItem ("Todas las fixturas", 1);
        targetKeys.add ({});

        int id = 2;
        for (const auto& f : playlist.getRig())
        {
            const juce::String key = ChoreographyEngine::fixtureKey (f);
            juce::String label = f.name;
            if (f.universe > 0 || f.startAddress > 0)
                label << "  (U" << f.universe << ":" << f.startAddress << ")";
            targetCombo.addItem (label, id++);
            targetKeys.add (key);
        }
    }

    juce::String targetShort (const juce::String& key) const
    {
        // key = name@uni:addr -> muestra solo el nombre.
        return key.upToFirstOccurrenceOf ("@", false, false);
    }

    void refreshList()
    {
        list.updateContent();
        list.repaint();
    }

    void startNew()
    {
        editIndex = -1;
        nameEditor.setText ("Nueva coreografia", juce::dontSendNotification);
        targetCombo.setSelectedId (1, juce::dontSendNotification);
        motionCombo.setSelectedId (16, juce::dontSendNotification);   // Auto IA por defecto
        colorCombo.setSelectedId (1, juce::dontSendNotification);     // Ciclo (identidad)
        colorBeatsSlider.setValue (1.0, juce::dontSendNotification);
        moveSpeedSlider.setValue (1.0, juce::dontSendNotification);
        list.deselectAllRows();
    }

    void loadRow (int row)
    {
        const auto& lib = playlist.getChoreoLibrary();
        if (row < 0 || row >= (int) lib.size())
            return;

        editIndex = row;
        const auto& c = lib[(size_t) row];
        nameEditor.setText (c.name, juce::dontSendNotification);

        int tIdx = targetKeys.indexOf (c.targetKey);
        if (tIdx < 0) tIdx = 0;
        targetCombo.setSelectedId (tIdx + 1, juce::dontSendNotification);

        motionCombo.setSelectedId ((int) c.style.motion + 1, juce::dontSendNotification);
        colorCombo.setSelectedId ((int) c.style.color + 1, juce::dontSendNotification);
        colorBeatsSlider.setValue (c.style.colorBeats, juce::dontSendNotification);
        moveSpeedSlider.setValue (c.style.moveSpeed, juce::dontSendNotification);
    }

    void commit()
    {
        ChoreographyEngine::Choreography c;
        c.name = nameEditor.getText().trim();
        if (c.name.isEmpty())
            c.name = "Coreografia";

        const int tIdx = juce::jlimit (0, targetKeys.size() - 1, targetCombo.getSelectedId() - 1);
        c.targetKey = targetKeys[tIdx];

        c.style.name       = c.name;
        c.style.motion     = (ChoreographyEngine::MotionStyle) juce::jmax (0, motionCombo.getSelectedId() - 1);
        c.style.color      = (ChoreographyEngine::ColorStyle)  juce::jmax (0, colorCombo.getSelectedId() - 1);
        c.style.colorBeats = colorBeatsSlider.getValue();
        c.style.moveSpeed  = moveSpeedSlider.getValue();

        if (editIndex >= 0)
            playlist.updateChoreo (editIndex, c);
        else
            editIndex = playlist.addChoreo (c);

        refreshList();
        list.selectRow (editIndex);
    }

    void deleteSelected()
    {
        const int row = list.getSelectedRow();
        if (row < 0)
            return;
        playlist.removeChoreo (row);
        startNew();
        refreshList();
    }

    //==============================================================================
    PlaylistManager& playlist;
    int editIndex = -1;

    juce::Label title, hint;
    juce::ListBox list;
    juce::TextButton newButton, deleteButton, saveButton;

    juce::Label nameLabel, targetLabel, motionLabel, colorLabel, colorBeatsLabel, moveSpeedLabel;
    juce::TextEditor nameEditor;
    juce::ComboBox targetCombo, motionCombo, colorCombo;
    juce::Slider colorBeatsSlider, moveSpeedSlider;
    juce::StringArray targetKeys;   // paralelo a targetCombo (id-1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoreoPanel)
};
