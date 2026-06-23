#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../source/FixtureModel.h"
#include "../../source/LuxLookAndFeel.h"
#include "PlaylistManager.h"

/**
    Pantalla "Equipos Custom": crea/edita plantillas de equipos propios.

    Para cada equipo defines cuantos canales tiene; y para cada canal su tipo,
    una descripcion general, y opcionalmente varios RANGOS de valor (000-255)
    con descripcion independiente (un mismo canal puede hacer cosas distintas
    segun el valor, p.ej. 0-127 abierto / 128-255 strobe).

    Las plantillas creadas se guardan en la libreria del PlaylistManager y luego
    se pueden anadir al rig desde la pantalla de asignacion de stems.
*/
class CustomFixturePanel : public juce::Component,
                           private juce::ListBoxModel
{
public:
    explicit CustomFixturePanel (PlaylistManager& pm) : playlist (pm)
    {
        using P = LuxLookAndFeel::Palette;

        title.setText ("Equipos Custom", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        hint.setText ("Crea equipos propios: define canales, descripcion y rangos de valor (000-255).",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        hint.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (hint);

        list.setModel (this);
        list.setRowHeight (26);
        addAndMakeVisible (list);

        newButton.setButtonText ("+ Nuevo");
        newButton.onClick = [this] { startNew(); };
        addAndMakeVisible (newButton);

        deleteButton.setButtonText ("Eliminar");
        deleteButton.onClick = [this] { deleteSelected(); };
        addAndMakeVisible (deleteButton);

        nameLabel.setText ("Nombre", juce::dontSendNotification);
        nameLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (nameLabel);
        addAndMakeVisible (nameEditor);
        nameEditor.setTextToShowWhenEmpty ("Mi equipo", juce::Colour (P::textDim));

        modelLabel.setText ("Modelo", juce::dontSendNotification);
        modelLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (modelLabel);
        addAndMakeVisible (modelEditor);
        modelEditor.setTextToShowWhenEmpty ("Opcional", juce::Colour (P::textDim));

        chanHeader.setText ("Canales", juce::dontSendNotification);
        chanHeader.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        chanHeader.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (chanHeader);

        addChannelButton.setButtonText ("+ Canal");
        addChannelButton.onClick = [this] { addChannel(); };
        addAndMakeVisible (addChannelButton);

        saveButton.setButtonText ("Guardar equipo");
        saveButton.onClick = [this] { commit(); };
        addAndMakeVisible (saveButton);

        viewport.setViewedComponent (&channelHolder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        startNew();
        refreshList();
    }

    /** Refresca la lista cuando cambia la libreria por fuera. */
    void refreshFromPlaylist()
    {
        refreshList();
    }

    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;
        g.fillAll (juce::Colour (P::bg1));

        // separador columna izquierda
        g.setColour (juce::Colour (P::line));
        g.drawVerticalLine (kLeftW, 8.0f, (float) getHeight() - 8.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);

        auto top = r.removeFromTop (28);
        title.setBounds (top.removeFromLeft (220));
        hint.setBounds (top);

        r.removeFromTop (8);

        // columna izquierda: lista de equipos custom + botones
        auto left = r.removeFromLeft (kLeftW - 12);
        auto leftButtons = left.removeFromBottom (30);
        newButton.setBounds (leftButtons.removeFromLeft (90));
        leftButtons.removeFromLeft (6);
        deleteButton.setBounds (leftButtons.removeFromLeft (90));
        left.removeFromBottom (8);
        list.setBounds (left);

        r.removeFromLeft (16);

        // columna derecha: editor
        auto right = r;

        auto nameRow = right.removeFromTop (26);
        nameLabel.setBounds (nameRow.removeFromLeft (60));
        nameEditor.setBounds (nameRow.removeFromLeft (juce::jmin (240, nameRow.getWidth())));
        right.removeFromTop (6);

        auto modelRow = right.removeFromTop (26);
        modelLabel.setBounds (modelRow.removeFromLeft (60));
        modelEditor.setBounds (modelRow.removeFromLeft (juce::jmin (240, modelRow.getWidth())));
        right.removeFromTop (10);

        auto chanRow = right.removeFromTop (26);
        chanHeader.setBounds (chanRow.removeFromLeft (120));
        addChannelButton.setBounds (chanRow.removeFromRight (90));
        right.removeFromTop (4);

        auto bottom = right.removeFromBottom (38);
        saveButton.setBounds (bottom.removeFromRight (150).withSizeKeepingCentre (150, 30));
        right.removeFromBottom (6);

        viewport.setBounds (right);
        layoutChannels();
    }

private:
    static constexpr int kLeftW = 230;

    //==============================================================================
    // Fila editable de un rango de valor dentro de un canal.
    struct RangeRow : public juce::Component
    {
        juce::TextEditor lowBox, highBox, descBox;
        juce::TextButton delButton { "x" };
        std::function<void()> onChange, onDelete;

        RangeRow()
        {
            using P = LuxLookAndFeel::Palette;
            for (auto* b : { &lowBox, &highBox })
            {
                b->setInputRestrictions (3, "0123456789");
                b->setJustification (juce::Justification::centred);
                addAndMakeVisible (b);
                b->onTextChange = [this] { if (onChange) onChange(); };
            }
            descBox.setTextToShowWhenEmpty ("Que hace en este rango", juce::Colour (P::textDim));
            descBox.onTextChange = [this] { if (onChange) onChange(); };
            addAndMakeVisible (descBox);

            delButton.onClick = [this] { if (onDelete) onDelete(); };
            addAndMakeVisible (delButton);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            lowBox.setBounds (r.removeFromLeft (44));
            r.removeFromLeft (4);
            auto sep = r.removeFromLeft (14);
            juce::ignoreUnused (sep);
            highBox.setBounds (r.removeFromLeft (44));
            r.removeFromLeft (6);
            delButton.setBounds (r.removeFromRight (26));
            r.removeFromRight (4);
            descBox.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            using P = LuxLookAndFeel::Palette;
            g.setColour (juce::Colour (P::textDim));
            g.setFont (12.0f);
            g.drawText ("-", 48, 0, 14, getHeight(), juce::Justification::centred);
        }
    };

    //==============================================================================
    // Fila editable de un canal: tipo + descripcion + rangos.
    struct ChannelRowComp : public juce::Component
    {
        juce::Label       numLabel;
        juce::ComboBox    typeCombo;
        juce::TextEditor  descBox;
        juce::TextButton  addRangeButton { "+ Rango" };
        juce::TextButton  delButton { "Quitar canal" };
        juce::Label       rangesLabel;
        juce::OwnedArray<RangeRow> rangeRows;

        ChannelDef* model = nullptr;
        int index = 0;
        std::function<void()> onStructureChange;   // anadir/quitar rango o canal
        std::function<void()> onDeleteChannel;

        ChannelRowComp()
        {
            using P = LuxLookAndFeel::Palette;

            numLabel.setColour (juce::Label::textColourId, juce::Colour (P::accent));
            numLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            addAndMakeVisible (numLabel);

            typeCombo.addItemList (allChannelTypeNames(), 1);
            typeCombo.onChange = [this]
            {
                if (model != nullptr)
                {
                    const bool wasDefault = (model->colour == defaultColourForChannelType (model->type));
                    model->type = channelTypeFromString (typeCombo.getText());
                    // Auto-color por tipo, salvo que el usuario lo hubiera personalizado.
                    if (wasDefault)
                        model->colour = defaultColourForChannelType (model->type);
                }
            };
            addAndMakeVisible (typeCombo);

            descBox.setTextToShowWhenEmpty ("Descripcion del canal", juce::Colour (P::textDim));
            descBox.onTextChange = [this] { if (model != nullptr) model->description = descBox.getText(); };
            addAndMakeVisible (descBox);

            rangesLabel.setText ("Rangos de valor:", juce::dontSendNotification);
            rangesLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
            rangesLabel.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (rangesLabel);

            addRangeButton.onClick = [this]
            {
                if (model == nullptr) return;
                model->ranges.push_back ({ 0, 255, juce::String() });
                if (onStructureChange) onStructureChange();
            };
            addAndMakeVisible (addRangeButton);

            delButton.onClick = [this] { if (onDeleteChannel) onDeleteChannel(); };
            addAndMakeVisible (delButton);
        }

        void bind (ChannelDef* m, int idx)
        {
            model = m;
            index = idx;
            numLabel.setText ("Canal " + juce::String (idx + 1), juce::dontSendNotification);
            typeCombo.setText (channelTypeToString (m->type), juce::dontSendNotification);
            descBox.setText (m->description, juce::dontSendNotification);
            rebuildRanges();
        }

        void rebuildRanges()
        {
            rangeRows.clear();
            if (model == nullptr) return;

            for (size_t i = 0; i < model->ranges.size(); ++i)
            {
                auto* rr = new RangeRow();
                rr->lowBox.setText (juce::String (model->ranges[i].low), juce::dontSendNotification);
                rr->highBox.setText (juce::String (model->ranges[i].high), juce::dontSendNotification);
                rr->descBox.setText (model->ranges[i].description, juce::dontSendNotification);

                const size_t ri = i;
                rr->onChange = [this, ri]
                {
                    if (model == nullptr || ri >= model->ranges.size()) return;
                    auto* row = rangeRows[(int) ri];
                    model->ranges[ri].low         = juce::jlimit (0, 255, row->lowBox.getText().getIntValue());
                    model->ranges[ri].high        = juce::jlimit (0, 255, row->highBox.getText().getIntValue());
                    model->ranges[ri].description = row->descBox.getText();
                };
                rr->onDelete = [this, ri]
                {
                    if (model == nullptr || ri >= model->ranges.size()) return;
                    model->ranges.erase (model->ranges.begin() + (long) ri);
                    if (onStructureChange) onStructureChange();
                };
                addAndMakeVisible (rr);
                rangeRows.add (rr);
            }
        }

        int preferredHeight() const
        {
            const int base = 34 + 28;                       // fila tipo + descripcion
            const int ranges = 22 + (int) rangeRows.size() * 28 + 28; // label + filas + boton
            return base + ranges + 12;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 6);

            auto headerRow = r.removeFromTop (28);
            numLabel.setBounds (headerRow.removeFromLeft (70));
            typeCombo.setBounds (headerRow.removeFromLeft (150));
            headerRow.removeFromLeft (8);
            delButton.setBounds (headerRow.removeFromRight (100));
            r.removeFromTop (6);

            descBox.setBounds (r.removeFromTop (26));
            r.removeFromTop (8);

            auto rl = r.removeFromTop (20);
            rangesLabel.setBounds (rl.removeFromLeft (130));
            r.removeFromTop (2);

            for (auto* rr : rangeRows)
            {
                rr->setBounds (r.removeFromTop (26).reduced (4, 0));
                r.removeFromTop (2);
            }
            r.removeFromTop (2);
            addRangeButton.setBounds (r.removeFromTop (24).removeFromLeft (90));
        }

        void paint (juce::Graphics& g) override
        {
            using P = LuxLookAndFeel::Palette;
            g.setColour (juce::Colour (P::surface));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
            g.setColour (juce::Colour (P::line));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 1.0f);
        }
    };

    //==============================================================================
    PlaylistManager& playlist;

    juce::Label      title, hint, nameLabel, modelLabel, chanHeader;
    juce::ListBox    list;
    juce::TextButton newButton, deleteButton, addChannelButton, saveButton;
    juce::TextEditor nameEditor, modelEditor;
    juce::Viewport   viewport;
    juce::Component  channelHolder;
    juce::OwnedArray<ChannelRowComp> channelRows;

    std::vector<ChannelDef> editChannels;   // canales del equipo en edicion
    int editingIndex = -1;                  // indice en la libreria (-1 = nuevo)

    //==============================================================================
    void startNew()
    {
        editingIndex = -1;
        editChannels.clear();
        ChannelDef c; c.type = ChannelType::Dimmer;
        c.colour = defaultColourForChannelType (c.type);
        editChannels.push_back (c);
        nameEditor.setText ("", juce::dontSendNotification);
        modelEditor.setText ("", juce::dontSendNotification);
        list.deselectAllRows();
        rebuildChannels();
    }

    void loadFromLibrary (int idx)
    {
        const auto& lib = playlist.getCustomFixtures();
        if (idx < 0 || idx >= (int) lib.size()) return;

        editingIndex = idx;
        const auto& f = lib[(size_t) idx];
        nameEditor.setText (f.name, juce::dontSendNotification);
        modelEditor.setText (f.model, juce::dontSendNotification);
        editChannels = f.channels;
        if (editChannels.empty())
        {
            ChannelDef c; editChannels.push_back (c);
        }
        rebuildChannels();
    }

    void addChannel()
    {
        ChannelDef c; c.type = ChannelType::Generic;
        c.colour = defaultColourForChannelType (c.type);
        editChannels.push_back (c);
        rebuildChannels();
    }

    void rebuildChannels()
    {
        channelRows.clear();
        for (int i = 0; i < (int) editChannels.size(); ++i)
        {
            auto* row = new ChannelRowComp();
            row->onStructureChange = [this] { rebuildChannels(); };
            const int ci = i;
            row->onDeleteChannel = [this, ci]
            {
                if (ci >= 0 && ci < (int) editChannels.size() && editChannels.size() > 1)
                {
                    editChannels.erase (editChannels.begin() + ci);
                    rebuildChannels();
                }
            };
            channelHolder.addAndMakeVisible (row);
            row->bind (&editChannels[(size_t) i], i);
            channelRows.add (row);
        }
        layoutChannels();
    }

    void layoutChannels()
    {
        const int w = juce::jmax (200, viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? 12 : 0));
        int y = 4;
        for (auto* row : channelRows)
        {
            const int h = row->preferredHeight();
            row->setBounds (4, y, w - 8, h);
            y += h + 8;
        }
        channelHolder.setSize (w, juce::jmax (y + 4, viewport.getHeight()));
    }

    void commit()
    {
        Fixture f;
        f.name  = nameEditor.getText().trim();
        if (f.name.isEmpty()) f.name = "Equipo custom";
        f.model = modelEditor.getText().trim();
        f.channels = editChannels;

        if (editingIndex >= 0)
            playlist.updateCustomFixture (editingIndex, f);
        else
        {
            playlist.addCustomFixture (f);
            editingIndex = (int) playlist.getCustomFixtures().size() - 1;
        }
        refreshList();
        list.selectRow (editingIndex);
    }

    void deleteSelected()
    {
        const int row = list.getSelectedRow();
        if (row < 0) return;
        playlist.removeCustomFixture (row);
        refreshList();
        startNew();
    }

    void refreshList()
    {
        list.updateContent();
        list.repaint();
    }

    //==============================================================================
    // ListBoxModel
    int getNumRows() override { return (int) playlist.getCustomFixtures().size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool selected) override
    {
        using P = LuxLookAndFeel::Palette;
        const auto& lib = playlist.getCustomFixtures();
        if (rowNumber < 0 || rowNumber >= (int) lib.size()) return;

        if (selected)
        {
            g.setColour (juce::Colour (P::accent).withAlpha (0.18f));
            g.fillRect (0, 0, width, height);
        }
        const auto& f = lib[(size_t) rowNumber];
        g.setColour (juce::Colour (selected ? P::textHi : P::textMid));
        g.setFont (13.0f);
        g.drawText (f.name + "  (" + juce::String (f.channelCount()) + "ch)",
                    8, 0, width - 12, height, juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        loadFromLibrary (row);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustomFixturePanel)
};
