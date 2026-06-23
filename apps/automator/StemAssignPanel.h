#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../source/FixtureModel.h"
#include "../../source/LuxLookAndFeel.h"
#include "PlaylistManager.h"
#include "ChoreographyEngine.h"

/**
    Pantalla "Stems": 4 cuadros (Bateria / Bajo / Voces / Melodia) donde listas
    que equipos controla cada stem en modo Stems IA.

    Cada cuadro permite anadir un equipo (desde las plantillas predefinidas o las
    custom) o quitar el seleccionado. Anadir crea el equipo en el rig con una
    direccion DMX libre y lo asigna a ese stem.
*/
class StemAssignPanel : public juce::Component,
                        public juce::DragAndDropContainer
{
public:
    explicit StemAssignPanel (PlaylistManager& pm) : playlist (pm)
    {
        using P = LuxLookAndFeel::Palette;

        title.setText ("Asignacion de Stems", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        hint.setText ("Que equipos controla cada instrumento. Arrastra un equipo de un cuadro a otro para cambiarlo de stem. Anade desde los predefinidos o tus equipos custom. Doble clic en un equipo para cambiar su direccion DMX.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        hint.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (hint);

        addBox (boxes[0], "drums",  "Bateria",  juce::Colour (0xffff5a5a));
        addBox (boxes[1], "bass",   "Bajo",     juce::Colour (0xff4f8cff));
        addBox (boxes[2], "vocals", "Voces",    juce::Colour (0xff4fdf78));
        addBox (boxes[3], "other",  "Melodia",  juce::Colour (0xffffb020));

        refreshAll();
    }

    /** Refresca el contenido de los 4 cuadros (tras cambios externos en el rig). */
    void refreshAll()
    {
        for (auto& b : boxes)
            b->refresh();
    }

    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;
        g.fillAll (juce::Colour (P::bg1));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);

        auto top = r.removeFromTop (28);
        title.setBounds (top.removeFromLeft (260));
        hint.setBounds (top);

        r.removeFromTop (10);

        const int n = (int) boxes.size();
        const int gap = 12;
        const int w = (r.getWidth() - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
        {
            auto col = r.removeFromLeft (w);
            boxes[(size_t) i]->setBounds (col);
            if (i < n - 1) r.removeFromLeft (gap);
        }
    }

private:
    //==============================================================================
    // Un cuadro de stem: titulo, lista de equipos asignados, botones anadir/quitar.
    struct StemBox : public juce::Component,
                     private juce::ListBoxModel,
                     public  juce::DragAndDropTarget
    {
        StemBox (PlaylistManager& pm, juce::String s, juce::String t, juce::Colour c)
            : playlist (pm), stem (std::move (s)), titleText (std::move (t)), accent (c)
        {
            using P = LuxLookAndFeel::Palette;

            heading.setText (titleText, juce::dontSendNotification);
            heading.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            heading.setColour (juce::Label::textColourId, accent);
            addAndMakeVisible (heading);

            list.setModel (this);
            list.setRowHeight (24);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colour (P::bg0));
            addAndMakeVisible (list);
            addButton.setButtonText ("+ Anadir");
            addButton.onClick = [this] { showAddMenu(); };
            addAndMakeVisible (addButton);

            removeButton.setButtonText ("Quitar");
            removeButton.onClick = [this] { removeSelected(); };
            addAndMakeVisible (removeButton);
        }

        void refresh()
        {
            indices = playlist.rigIndicesForStem (stem);
            list.updateContent();
            list.repaint();
        }

        void paint (juce::Graphics& g) override
        {
            using P = LuxLookAndFeel::Palette;
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (P::surface));
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (dragOver ? accent : accent.withAlpha (0.45f));
            g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, dragOver ? 2.2f : 1.2f);

            // resaltado al arrastrar un equipo encima
            if (dragOver)
            {
                g.setColour (accent.withAlpha (0.10f));
                g.fillRoundedRectangle (b.reduced (1.5f), 8.0f);
            }

            // franja de color superior
            g.setColour (accent.withAlpha (0.18f));
            g.fillRoundedRectangle (b.removeFromTop (32.0f).reduced (1.0f), 8.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            heading.setBounds (r.removeFromTop (26));
            r.removeFromTop (4);

            auto buttons = r.removeFromBottom (28);
            addButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
            buttons.removeFromLeft (6);
            removeButton.setBounds (buttons);
            r.removeFromBottom (6);

            list.setBounds (r);
        }

        void showAddMenu()
        {
            const auto templates = playlist.getFixtureTemplates();
            juce::PopupMenu menu;

            juce::PopupMenu predef, custom;
            const auto predefList = PlaylistManager::predefinedTemplates();
            const int predefCount = (int) predefList.size();

            for (int i = 0; i < (int) templates.size(); ++i)
            {
                const auto& f = templates[(size_t) i];
                const juce::String label = f.name + "  (" + juce::String (f.channelCount()) + "ch)";
                if (i < predefCount)
                    predef.addItem (i + 1, label);
                else
                    custom.addItem (i + 1, label);
            }

            menu.addSubMenu ("Predefinidos", predef);
            if (custom.getNumItems() > 0)
                menu.addSubMenu ("Custom", custom);
            else
                menu.addItem (-1, "(sin equipos custom)", false);

            auto self = juce::Component::SafePointer<StemBox> (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                [self, templates] (int result)
                {
                    if (self == nullptr || result <= 0) return;
                    const int idx = result - 1;
                    if (idx >= 0 && idx < (int) templates.size())
                    {
                        self->playlist.addFixtureToRig (templates[(size_t) idx], self->stem);
                        if (self->onChanged) self->onChanged();
                    }
                });
        }

        void removeSelected()
        {
            const int row = list.getSelectedRow();
            if (row < 0 || row >= (int) indices.size()) return;

            const auto& rig = playlist.getRig();
            const int fixIdx = indices[(size_t) row];
            if (fixIdx < 0 || fixIdx >= (int) rig.size()) return;

            const auto key = ChoreographyEngine::fixtureKey (rig[(size_t) fixIdx]);
            playlist.removeRigFixture (key);
            if (onChanged) onChanged();
        }

        /** Abre un dialogo para cambiar el universo/direccion DMX del equipo.
            Rechaza direcciones que choquen con otro equipo o se salgan de 1-512. */
        void editAddress (int row)
        {
            if (row < 0 || row >= (int) indices.size()) return;
            const auto& rig = playlist.getRig();
            const int fixIdx = indices[(size_t) row];
            if (fixIdx < 0 || fixIdx >= (int) rig.size()) return;

            const auto& f    = rig[(size_t) fixIdx];
            const int chCount = f.channelCount();
            const auto key    = ChoreographyEngine::fixtureKey (f);

            auto* aw = new juce::AlertWindow (
                "Direccion de " + f.name,
                "Ocupa " + juce::String (chCount) + " canal(es). La direccion de inicio debe "
                "estar entre 1 y 512 y no chocar con otro equipo.",
                juce::MessageBoxIconType::NoIcon);

            aw->addTextEditor ("uni",  juce::String (f.universe),     "Universo:");
            aw->addTextEditor ("addr", juce::String (f.startAddress), "Direccion inicio:");
            aw->addButton ("Guardar",  1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            auto self = juce::Component::SafePointer<StemBox> (this);
            aw->enterModalState (true, juce::ModalCallbackFunction::create (
                [self, aw, key, chCount] (int res)
                {
                    std::unique_ptr<juce::AlertWindow> hold (aw);
                    if (self == nullptr || res != 1) return;

                    const int uni  = aw->getTextEditorContents ("uni").getIntValue();
                    const int addr = aw->getTextEditorContents ("addr").getIntValue();

                    if (! self->playlist.setRigFixtureAddress (key, uni, addr))
                    {
                        const int endAddr = addr + chCount - 1;
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon,
                            "Direccion ocupada",
                            "El rango " + juce::String (addr) + "-" + juce::String (endAddr)
                            + " (universo " + aw->getTextEditorContents ("uni")
                            + ") choca con otro equipo o se sale de 1-512. Elige otra direccion.");
                        return;
                    }
                    if (self->onChanged) self->onChanged();
                }), false);
        }

        // ListBoxModel
        int getNumRows() override { return (int) indices.size(); }

        /** Inicia el arrastre del equipo: la descripcion es su clave estable. */
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
        {
            if (rows.isEmpty()) return {};
            const int row = rows[0];
            const auto& rig = playlist.getRig();
            if (row < 0 || row >= (int) indices.size()) return {};
            const int fixIdx = indices[(size_t) row];
            if (fixIdx < 0 || fixIdx >= (int) rig.size()) return {};
            return "stemfix:" + ChoreographyEngine::fixtureKey (rig[(size_t) fixIdx]);
        }

        // DragAndDropTarget: aceptar equipos arrastrados desde otro cuadro.
        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return d.description.toString().startsWith ("stemfix:");
        }

        void itemDragEnter (const SourceDetails&) override { dragOver = true;  repaint(); }
        void itemDragExit  (const SourceDetails&) override { dragOver = false; repaint(); }

        void itemDropped (const SourceDetails& d) override
        {
            dragOver = false;
            repaint();
            const juce::String key = d.description.toString().fromFirstOccurrenceOf ("stemfix:", false, false);
            if (key.isEmpty()) return;
            if (playlist.getStemFor (key) == stem) return;   // ya esta en este stem
            playlist.setStemFor (key, stem);
            if (onChanged) onChanged();
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            editAddress (row);
        }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            using P = LuxLookAndFeel::Palette;
            const auto& rig = playlist.getRig();
            if (row < 0 || row >= (int) indices.size()) return;
            const int fixIdx = indices[(size_t) row];
            if (fixIdx < 0 || fixIdx >= (int) rig.size()) return;

            if (selected)
            {
                g.setColour (accent.withAlpha (0.22f));
                g.fillRect (0, 0, width, height);
            }
            const auto& f = rig[(size_t) fixIdx];
            g.setColour (juce::Colour (selected ? P::textHi : P::textMid));
            g.setFont (12.5f);
            const juce::String addr = "U" + juce::String (f.universe) + ":" + juce::String (f.startAddress);
            g.drawText (f.name, 6, 0, width - 70, height, juce::Justification::centredLeft);
            g.setColour (juce::Colour (P::textDim));
            g.drawText (addr, width - 64, 0, 58, height, juce::Justification::centredRight);
        }

        PlaylistManager&  playlist;
        juce::String      stem, titleText;
        juce::Colour      accent;
        juce::Label       heading;
        juce::ListBox     list;
        juce::TextButton  addButton, removeButton;
        std::vector<int>  indices;
        bool              dragOver = false;
        std::function<void()> onChanged;
    };

    void addBox (std::unique_ptr<StemBox>& slot, juce::String stem, juce::String boxTitle, juce::Colour c)
    {
        slot = std::make_unique<StemBox> (playlist, std::move (stem), std::move (boxTitle), c);
        slot->onChanged = [this] { refreshAll(); if (onAssignmentsChanged) onAssignmentsChanged(); };
        addAndMakeVisible (slot.get());
    }

    PlaylistManager& playlist;
    juce::Label title, hint;
    std::array<std::unique_ptr<StemBox>, 4> boxes;

public:
    std::function<void()> onAssignmentsChanged;   // avisa al host para refrescar el show/escenario

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemAssignPanel)
};
