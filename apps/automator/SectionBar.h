#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TrackAnalysis.h"
#include "../../source/LuxLookAndFeel.h"

/**
    Franja que visualiza la ESTRUCTURA detectada del tema (intro / subida / drop /
    verso / break / outro) como bloques de color a lo largo del tiempo, con su
    etiqueta y un cursor de reproduccion. Clic = saltar a esa parte de la cancion.

    Se alimenta con setSections() (de TrackAnalysis::sections) y setPosition() en
    cada tick del transporte. Si no hay secciones, queda en blanco (no estorba).
*/
class SectionBar : public juce::Component
{
public:
    using P = LuxLookAndFeel::Palette;

    SectionBar()
    {
        setInterceptsMouseClicks (true, false);
    }

    /** Carga las secciones del tema activo + su duracion total. */
    void setSections (const std::vector<TrackSection>& s, double lengthSeconds)
    {
        sections = s;
        lengthSec = lengthSeconds;
        repaint();
    }

    void clearSections()
    {
        sections.clear();
        lengthSec = 0.0;
        repaint();
    }

    /** Posicion de reproduccion (segundos). */
    void setPosition (double seconds)
    {
        if (std::abs (seconds - posSec) < 0.02)
            return;
        posSec = seconds;
        repaint();
    }

    bool hasSections() const noexcept { return ! sections.empty() && lengthSec > 0.0; }

    /** Callback al hacer clic: fraccion 0..1 de la cancion para saltar. */
    std::function<void (double)> onSeek;

    /** Callback cuando el usuario edita las secciones (mover borde, anadir,
        dividir, cambiar tipo, eliminar). Se entrega la lista resultante para
        que el host la persista. */
    std::function<void (const std::vector<TrackSection>&)> onSectionsEdited;

    /** Activa/desactiva la edicion manual (arrastrar bordes, clic derecho). */
    void setEditable (bool shouldBeEditable) { editable = shouldBeEditable; repaint(); }

    //==============================================================================
    /** Color caracteristico de cada tipo de seccion. */
    static juce::Colour colourForType (int type)
    {
        switch (type)
        {
            case TrackSection::Intro: return juce::Colour (0xff2a6cff).withAlpha (0.55f);
            case TrackSection::Build: return juce::Colour (0xffffa020);
            case TrackSection::Drop:  return juce::Colour (0xffff3b6b);
            case TrackSection::Verse: return juce::Colour (0xff20b8a8);
            case TrackSection::Break: return juce::Colour (0xff6b54c8).withAlpha (0.75f);
            case TrackSection::Outro: return juce::Colour (0xff2a6cff).withAlpha (0.40f);
            case TrackSection::Chorus: return juce::Colour (0xffffe14d);
            default:                  return juce::Colour (P::control);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);

        // Fondo del carril.
        g.setColour (juce::Colour (P::bg0));
        g.fillRoundedRectangle (r, 3.0f);

        if (! hasSections())
        {
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Sin estructura analizada", getLocalBounds(),
                        juce::Justification::centred);
            return;
        }

        const float w = r.getWidth();
        const float x0 = r.getX();
        const float top = r.getY();
        const float h = r.getHeight();

        const double posFrac = lengthSec > 0.0 ? juce::jlimit (0.0, 1.0, posSec / lengthSec) : 0.0;

        for (const auto& s : sections)
        {
            const float xa = x0 + (float) juce::jlimit (0.0, 1.0, s.startSec / lengthSec) * w;
            const float xb = x0 + (float) juce::jlimit (0.0, 1.0, s.endSec   / lengthSec) * w;
            const float bw = juce::jmax (1.0f, xb - xa);

            juce::Rectangle<float> block (xa, top, bw, h);
            const bool active = (posSec >= s.startSec && posSec < s.endSec);

            // El brillo del bloque sigue el nivel de energia de la seccion.
            const float lvl = juce::jlimit (0.0f, 1.0f, s.level);
            juce::Colour base = colourForType (s.type);
            juce::Colour fill = base.withMultipliedBrightness (0.55f + 0.45f * lvl)
                                    .withMultipliedAlpha (active ? 1.0f : 0.82f);

            g.setColour (fill);
            g.fillRect (block.reduced (0.6f, 0.0f));

            // Borde superior segun energia (linea que "sube" con la intensidad).
            g.setColour (base.brighter (0.4f).withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (xa + 0.6f, top, bw - 1.2f,
                                                juce::jmax (1.5f, h * (0.10f + 0.30f * lvl))));

            if (active)
            {
                g.setColour (juce::Colour (P::textHi).withAlpha (0.85f));
                g.drawRect (block.reduced (0.6f, 0.0f), 1.2f);
            }

            // Etiqueta si cabe.
            if (bw > 34.0f)
            {
                g.setColour (juce::Colour (0xff0a0c10).withAlpha (0.85f));
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (TrackSection::typeName (s.type),
                            block.toNearestInt().reduced (3, 0),
                            juce::Justification::centredLeft, true);
            }
        }

        // Cursor de reproduccion.
        const float px = x0 + (float) posFrac * w;
        g.setColour (juce::Colour (P::textHi));
        g.fillRect (px - 0.75f, top, 1.5f, h);

        // Tiradores de borde (solo en modo edicion): lineas finas entre secciones.
        if (editable)
        {
            for (size_t i = 0; i + 1 < sections.size(); ++i)
            {
                const float bx = timeToX (sections[i].endSec);
                const bool hot = ((int) i == draggingBoundary || (int) i == hoverBoundary);
                g.setColour (juce::Colour (P::textHi).withAlpha (hot ? 0.95f : 0.45f));
                g.fillRect (bx - (hot ? 1.5f : 0.75f), top, hot ? 3.0f : 1.5f, h);
                if (hot)
                {
                    // Pequenas flechas de "redimensionar".
                    g.fillRect (bx - 5.0f, top + h * 0.5f - 0.75f, 10.0f, 1.5f);
                }
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! editable || ! hasSections())
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }
        const int b = boundaryNear (e.position.x);
        if (b != hoverBoundary)
        {
            hoverBoundary = b;
            repaint();
        }
        setMouseCursor (b >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                               : juce::MouseCursor::PointingHandCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverBoundary != -1) { hoverBoundary = -1; repaint(); }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (editable && hasSections() && e.mods.isPopupMenu())
        {
            showContextMenu (e);
            return;
        }

        if (editable && hasSections())
        {
            const int b = boundaryNear (e.position.x);
            if (b >= 0)
            {
                draggingBoundary = b;
                repaint();
                return;          // arrastrando un borde: no saltar
            }
        }

        seekFromMouse (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingBoundary >= 0)
        {
            dragBoundaryTo (e.position.x);
            return;
        }
        seekFromMouse (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingBoundary >= 0)
        {
            draggingBoundary = -1;
            commitEdit();
            repaint();
        }
    }

private:
    float timeToX (double t) const
    {
        const float w = (float) getWidth();
        return lengthSec > 0.0 ? (float) (juce::jlimit (0.0, 1.0, t / lengthSec)) * w : 0.0f;
    }

    double xToTime (float x) const
    {
        const float w = (float) getWidth();
        return w > 0.0f ? juce::jlimit (0.0, lengthSec, (double) x / w * lengthSec) : 0.0;
    }

    /** Indice de borde interno (entre seccion i e i+1) cercano a x, o -1. */
    int boundaryNear (float x) const
    {
        int best = -1;
        float bestDist = 5.0f;   // tolerancia en px
        for (size_t i = 0; i + 1 < sections.size(); ++i)
        {
            const float bx = timeToX (sections[i].endSec);
            const float d = std::abs (x - bx);
            if (d < bestDist) { bestDist = d; best = (int) i; }
        }
        return best;
    }

    /** Indice de la seccion que contiene el tiempo dado, o -1. */
    int sectionAt (double t) const
    {
        for (size_t i = 0; i < sections.size(); ++i)
            if (t >= sections[i].startSec && t < sections[i].endSec)
                return (int) i;
        return sections.empty() ? -1 : (int) sections.size() - 1;
    }

    void dragBoundaryTo (float x)
    {
        const int i = draggingBoundary;
        if (i < 0 || i + 1 >= (int) sections.size())
            return;

        const double minDur = 0.30;   // segundos minimos por seccion
        const double lo = sections[(size_t) i].startSec   + minDur;
        const double hi = sections[(size_t) i + 1].endSec - minDur;
        const double t  = juce::jlimit (lo, juce::jmax (lo, hi), xToTime (x));

        sections[(size_t) i].endSec       = t;
        sections[(size_t) i + 1].startSec = t;
        repaint();
    }

    void commitEdit()
    {
        if (onSectionsEdited)
            onSectionsEdited (sections);
    }

    void showContextMenu (const juce::MouseEvent& e)
    {
        const double t = xToTime (e.position.x);
        const int idx = sectionAt (t);
        if (idx < 0)
            return;

        juce::PopupMenu typeMenu;
        for (int ty = TrackSection::Intro; ty <= TrackSection::Chorus; ++ty)
            typeMenu.addItem (200 + ty, TrackSection::typeName (ty), true,
                              sections[(size_t) idx].type == ty);

        // Submenu para INSERTAR una seccion nueva aqui, eligiendo su tipo de los
        // existentes (intro/subida/drop/verso/break/outro/chorus).
        juce::PopupMenu addMenu;
        for (int ty = TrackSection::Intro; ty <= TrackSection::Chorus; ++ty)
            addMenu.addItem (300 + ty, TrackSection::typeName (ty));

        juce::PopupMenu m;
        m.addSectionHeader (TrackSection::typeName (sections[(size_t) idx].type)
                            + "  (" + timeString (sections[(size_t) idx].startSec)
                            + " - " + timeString (sections[(size_t) idx].endSec) + ")");
        m.addSubMenu ("Cambiar tipo", typeMenu);
        m.addSubMenu ("Anadir seccion aqui", addMenu);
        m.addItem (2, "Dividir aqui");
        m.addItem (3, "Eliminar seccion", sections.size() > 1);

        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this, idx, t] (int r)
            {
                if (r == 0)
                    return;
                if (r >= 300)
                {
                    addSection (idx, t, r - 300);
                }
                else if (r >= 200)
                {
                    sections[(size_t) idx].type = r - 200;
                }
                else if (r == 2)
                {
                    splitSection (idx, t);
                }
                else if (r == 3)
                {
                    removeSection (idx);
                }
                repaint();
                commitEdit();
            });
    }

    void splitSection (int idx, double t)
    {
        if (idx < 0 || idx >= (int) sections.size())
            return;
        auto& s = sections[(size_t) idx];
        const double minDur = 0.30;
        if (t <= s.startSec + minDur || t >= s.endSec - minDur)
            return;   // muy cerca de un borde, no dividir

        TrackSection right = s;     // copia tipo/nivel
        right.startSec = t;
        right.endSec   = s.endSec;
        s.endSec       = t;
        sections.insert (sections.begin() + idx + 1, right);
    }

    /** Inserta una nueva seccion del tipo dado a partir de t, partiendo la
        seccion idx (la parte derecha pasa a ser la nueva con ese tipo). */
    void addSection (int idx, double t, int type)
    {
        if (idx < 0 || idx >= (int) sections.size())
            return;
        auto& s = sections[(size_t) idx];
        const double minDur = 0.30;

        // Si el clic cae casi al inicio de la seccion, solo cambia su tipo.
        if (t <= s.startSec + minDur)
        {
            s.type = type;
            return;
        }

        double endNew = s.endSec;
        // Si cabe, dejamos un trozo de la seccion original a la derecha tambien;
        // si no, la nueva ocupa hasta el final de la seccion.
        const bool roomRight = (s.endSec - t) > minDur;

        TrackSection nueva = s;     // hereda nivel
        nueva.type     = type;
        nueva.startSec = t;
        nueva.endSec   = endNew;

        s.endSec = t;               // la original termina donde empieza la nueva
        sections.insert (sections.begin() + idx + 1, nueva);

        juce::ignoreUnused (roomRight);
    }

    void removeSection (int idx)
    {
        if (idx < 0 || idx >= (int) sections.size() || sections.size() <= 1)
            return;
        const double a = sections[(size_t) idx].startSec;
        const double b = sections[(size_t) idx].endSec;
        sections.erase (sections.begin() + idx);
        // Rellenar el hueco: el vecino anterior se alarga, o el siguiente.
        if (idx - 1 >= 0)
            sections[(size_t) idx - 1].endSec = b;
        else if (idx < (int) sections.size())
            sections[(size_t) idx].startSec = a;
    }

    static juce::String timeString (double sec)
    {
        const int total = (int) (sec + 0.5);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    void seekFromMouse (const juce::MouseEvent& e)
    {
        if (! hasSections() || ! onSeek)
            return;
        const float w = (float) getWidth();
        const double frac = w > 0.0f ? juce::jlimit (0.0, 1.0, (double) e.position.x / w) : 0.0;
        onSeek (frac);
    }

    std::vector<TrackSection> sections;
    double lengthSec = 0.0;
    double posSec    = 0.0;

    bool editable        = false;
    int  draggingBoundary = -1;
    int  hoverBoundary    = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionBar)
};
