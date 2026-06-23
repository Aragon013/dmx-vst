#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/LuxLookAndFeel.h"
#include "ChoreographyEngine.h"
#include "PlaylistManager.h"
#include <vector>
#include <functional>

/**
    Ventana de COREOGRAFIAS POR CANCION, organizada POR EQUIPO.

    Se abre desde el Reproductor con un tema seleccionado. Muestra una seccion por
    cada equipo del escenario; en cada una puedes anadir o quitar las coreografias
    (de la libreria: custom o de fabrica) que sonaran en ESE equipo durante el tema.
    El Auto IA reparte las elegidas por energia (de lo calmo al climax).

    La configuracion se liga por cancion. Un boton permite COPIARLA a todas las
    canciones de la lista de reproduccion actual (las sobreescribe).

    Trabaja en vivo sobre el PlaylistManager: cada cambio regenera el show al instante.
*/
class SongChoreoPanel : public juce::Component
{
public:
    std::function<void()> onChanged;   // avisa al editor para refrescar el escenario

    SongChoreoPanel (PlaylistManager& pm,
                     juce::String songPath,
                     juce::String songName,
                     juce::StringArray playlistPaths,
                     juce::String playlistName)
        : playlist (pm),
          path (std::move (songPath)),
          listPaths (std::move (playlistPaths)),
          listName (std::move (playlistName))
    {
        using P = LuxLookAndFeel::Palette;

        title.setText ("Coreografias  ·  " + songName, juce::dontSendNotification);
        title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        hint.setText ("Anade o quita coreografias por equipo. El Auto IA las reparte por energia. "
                      "Crea coreografias nuevas en la pestana \"Creador\".",
                      juce::dontSendNotification);
        hint.setFont (juce::FontOptions (12.0f));
        hint.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);

        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        manageButton.setButtonText ("Eliminar de la biblioteca...");
        manageButton.setColour (juce::TextButton::buttonColourId, juce::Colour (P::control));
        manageButton.onClick = [this] { showDeleteMenu(); };
        addAndMakeVisible (manageButton);

        copyButton.setButtonText ("Copiar a la lista \"" + listName + "\"");
        copyButton.setColour (juce::TextButton::buttonColourId, juce::Colour (P::accent));
        copyButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::bg0));
        copyButton.onClick = [this] { confirmCopy(); };
        addAndMakeVisible (copyButton);

        closeButton.setButtonText ("Cerrar");
        closeButton.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };
        addAndMakeVisible (closeButton);

        rebuild();
        setSize (580, 600);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (LuxLookAndFeel::Palette::bg1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
        hint.setBounds (area.removeFromTop (38));
        area.removeFromTop (8);

        auto buttons = area.removeFromBottom (34);
        manageButton.setBounds (buttons.removeFromLeft (210));
        closeButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        copyButton.setBounds (buttons.removeFromRight (juce::jmin (240, buttons.getWidth())));
        area.removeFromBottom (8);

        viewport.setBounds (area);
        layoutSections();
    }

private:
    //==============================================================================
    /** Una "chapa" (chip) que representa una coreografia asignada a un equipo. */
    struct ChoreoChip : public juce::TextButton
    {
        ChoreoChip (const juce::String& n) : juce::TextButton (n + "   x"), choreoName (n)
        {
            using P = LuxLookAndFeel::Palette;
            setColour (juce::TextButton::buttonColourId, juce::Colour (P::controlHi));
            setColour (juce::TextButton::textColourOffId, juce::Colour (P::textHi));
            setTooltip ("Clic para quitar \"" + n + "\" de este equipo");
        }
        juce::String choreoName;
    };

    /** Seccion de UN equipo: cabecera + chips de coreografias + boton anadir. */
    struct FixtureSection : public juce::Component
    {
        FixtureSection (SongChoreoPanel& o, const Fixture& f)
            : owner (o), fixtureKey (ChoreographyEngine::fixtureKey (f)), fixtureName (f.name)
        {
            using P = LuxLookAndFeel::Palette;
            header.setText (f.name + "    U" + juce::String (f.universe) + ":" + juce::String (f.startAddress),
                            juce::dontSendNotification);
            header.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            header.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
            addAndMakeVisible (header);

            addButton.setButtonText ("+ Anadir");
            addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (P::control));
            addButton.onClick = [this] { showAddMenu(); };
            addAndMakeVisible (addButton);

            rebuildChips();
        }

        void rebuildChips()
        {
            chips.clear();
            const auto names = owner.playlist.getSongChoreosForFixture (owner.path, fixtureKey);
            for (const auto& n : names)
            {
                auto* chip = chips.add (new ChoreoChip (n));
                chip->onClick = [this, n] { removeOne (n); };
                addAndMakeVisible (chip);
            }
            resized();
        }

        void paint (juce::Graphics& g) override
        {
            using P = LuxLookAndFeel::Palette;
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colour (P::surface));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colour (P::line));
            g.drawRoundedRectangle (r, 8.0f, 1.0f);

            if (chips.isEmpty())
            {
                g.setColour (juce::Colour (P::textDim));
                g.setFont (juce::FontOptions (12.0f));
                g.drawText ("Sin coreografias  (usa el estilo global)", chipArea.translated (4, 0),
                            juce::Justification::centredLeft);
            }
        }

        /** Coloca cabecera, chips (con wrap) y el boton anadir. Devuelve alto usado. */
        int layout (int width, bool apply)
        {
            const int pad = 12, headerH = 22, chipH = 26, gap = 6;
            if (apply) header.setBounds (pad, 8, width - pad * 2, headerH);

            int x = pad, y = 8 + headerH + 6;
            const int lineH = chipH + gap;
            auto place = [&] (juce::Component* c, int w)
            {
                if (x > pad && x + w > width - pad) { x = pad; y += lineH; }
                if (apply) c->setBounds (x, y, w, chipH);
                x += w + gap;
            };

            for (auto* chip : chips)
                place (chip, chipWidth (chip->choreoName));
            place (&addButton, 96);

            if (apply)
                chipArea = juce::Rectangle<int> (pad, 8 + headerH + 6, width - pad * 2, chipH);

            return y + chipH + 10;
        }

        void resized() override { layout (getWidth(), true); }
        int  preferredHeight (int width) { return layout (width, false); }

    private:
        static int chipWidth (const juce::String& name)
        {
            return juce::jlimit (90, 240, 28 + name.length() * 8);
        }

        void removeOne (const juce::String& name)
        {
            auto names = owner.playlist.getSongChoreosForFixture (owner.path, fixtureKey);
            names.removeString (name);
            owner.playlist.setSongChoreosForFixture (owner.path, fixtureKey, names);
            owner.notifyChanged();
            rebuildChips();
            owner.relayoutAndRepaint();
        }

        void showAddMenu()
        {
            const auto& lib = owner.playlist.getChoreoLibrary();
            const auto assigned = owner.playlist.getSongChoreosForFixture (owner.path, fixtureKey);

            juce::PopupMenu menu;
            int added = 0;
            for (int i = 0; i < (int) lib.size(); ++i)
            {
                const auto& c = lib[(size_t) i];
                if (assigned.contains (c.name))
                    continue;
                juce::String tag = c.manual ? "  [manual]" : "  [" + c.style.name + "]";
                menu.addItem (i + 1, c.name + tag);
                ++added;
            }
            if (added == 0)
                menu.addItem (-1, "(no hay mas coreografias)", false);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
                [this] (int res)
                {
                    if (res <= 0) return;
                    const auto& lib2 = owner.playlist.getChoreoLibrary();
                    if (res - 1 >= (int) lib2.size()) return;
                    auto names = owner.playlist.getSongChoreosForFixture (owner.path, fixtureKey);
                    names.addIfNotAlreadyThere (lib2[(size_t) (res - 1)].name);
                    owner.playlist.setSongChoreosForFixture (owner.path, fixtureKey, names);
                    owner.notifyChanged();
                    rebuildChips();
                    owner.relayoutAndRepaint();
                });
        }

        SongChoreoPanel&  owner;
        juce::String      fixtureKey, fixtureName;
        juce::Label       header;
        juce::TextButton  addButton;
        juce::OwnedArray<ChoreoChip> chips;
        juce::Rectangle<int> chipArea;
    };

    //==============================================================================
    void rebuild()
    {
        sections.clear();
        for (const auto& f : playlist.getRig())
        {
            auto* s = sections.add (new FixtureSection (*this, f));
            holder.addAndMakeVisible (s);
        }
        layoutSections();
    }

    void layoutSections()
    {
        const int w = juce::jmax (200, viewport.getWidth() - 14);
        int y = 0;
        for (auto* s : sections)
        {
            const int h = s->preferredHeight (w);
            s->setBounds (0, y, w, h);
            y += h + 10;
        }
        holder.setBounds (0, 0, w, juce::jmax (viewport.getHeight(), y));
    }

    void relayoutAndRepaint()
    {
        layoutSections();
        repaint();
    }

    void notifyChanged() { if (onChanged) onChanged(); }

    void showDeleteMenu()
    {
        const auto& lib = playlist.getChoreoLibrary();
        if (lib.empty())
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Biblioteca de coreografias", "Aun no hay coreografias en la biblioteca.");
            return;
        }

        juce::PopupMenu menu;
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& c = lib[(size_t) i];
            juce::String tag = c.manual ? "  [manual]" : "  [" + c.style.name + "]";
            menu.addItem (i + 1, c.name + tag);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (manageButton),
            [this] (int res)
            {
                if (res <= 0) return;
                const auto& lib2 = playlist.getChoreoLibrary();
                if (res - 1 >= (int) lib2.size()) return;
                const auto name = lib2[(size_t) (res - 1)].name;
                juce::NativeMessageBox::showYesNoBox (juce::MessageBoxIconType::WarningIcon,
                    "Eliminar coreografia",
                    "Eliminar \"" + name + "\" de la biblioteca?\n\n"
                    "Se quitara de TODAS las canciones que la usen.",
                    this, juce::ModalCallbackFunction::create ([this, res] (int r)
                    {
                        if (r != 1) return;
                        playlist.removeChoreo (res - 1);
                        notifyChanged();
                        for (auto* s : sections) s->rebuildChips();
                        relayoutAndRepaint();
                    }));
            });
    }

    void confirmCopy()
    {
        int targets = 0;
        for (const auto& p : listPaths)
            if (p != path) ++targets;

        if (targets == 0)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Copiar coreografias",
                "No hay otras canciones en la lista \"" + listName + "\" a las que copiar.");
            return;
        }

        juce::NativeMessageBox::showYesNoBox (juce::MessageBoxIconType::WarningIcon,
            "Copiar coreografias",
            "Estas realmente seguro que quieres copiar las coreografias de esta cancion al resto "
            "de la lista de reproduccion \"" + listName + "\"?\n\n"
            "Esto eliminara y/o agregara las coreografias a cada una de las " + juce::String (targets)
            + " cancion(es) de la lista.",
            this, juce::ModalCallbackFunction::create ([this] (int r)
            {
                if (r != 1) return;
                playlist.copySongChoreosToPaths (path, listPaths);
                notifyChanged();
                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    "Copiar coreografias",
                    "Coreografias copiadas a la lista \"" + listName + "\".");
            }));
    }

    PlaylistManager&  playlist;
    juce::String      path;
    juce::StringArray listPaths;
    juce::String      listName;

    juce::Label       title, hint;
    juce::Viewport    viewport;
    juce::Component   holder;
    juce::OwnedArray<FixtureSection> sections;
    juce::TextButton  manageButton, copyButton, closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongChoreoPanel)
};
