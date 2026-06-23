#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PlaybackEngine.h"
#include "DmxShow.h"
#include "MotionFigure.h"
#include "../../source/LuxLookAndFeel.h"
#include <cmath>
#include <map>

/**
    Visualizador de escenario del AI Automator: dibuja una representacion
    SIMULADA de las luces del show (no medidores, sino los propios aparatos y
    sus haces de luz), leyendo el ultimo frame DMX del PlaybackEngine.

    Reconoce por su nombre/modelo/canales el tipo de cada equipo y lo pinta con
    una forma reconocible y su haz correspondiente:
      - PAR        -> foco frontal con halo de color (suelo, ilumina hacia arriba)
      - Wash       -> bano de luz ancho y suave desde la barra superior
      - Barra LED  -> barra horizontal segmentada con su color
      - Cabeza     -> cabeza movil con haz fino orientado por Pan/Tilt
      - Spider     -> araña con varios haces finos en abanico
      - Strobe     -> destello blanco intenso al dispararse

    No tiene timer propio: el componente padre llama refreshFrom() (misma API
    que DmxPreview) desde el timer de UI.
*/
class StageVisualizer : public juce::Component
{
public:
    StageVisualizer() { loadLayout(); }

    enum class Kind { Par, Wash, Bar, MovingHead, Spider, Strobe, Generic };

    /** Llamado al elegir un stem para un equipo (clave, stem). stem "" = automatico. */
    std::function<void (const juce::String& key, const juce::String& stem)> onAssignStem;

    /** Devuelve el stem asignado a un equipo por su clave ("" = automatico). */
    std::function<juce::String (const juce::String& key)> getAssignedStem;

    /** Hay un tema activo al que poder asignar stems SOLO para esa cancion. */
    std::function<bool()> songAssignAvailable;
    /** Nombre del tema activo (para el encabezado del menu por cancion). */
    std::function<juce::String()> activeSongName;
    /** Stem que el tema activo asigna a un equipo (propio o, si no, el global). */
    std::function<juce::String (const juce::String& key)> getSongStemFor;
    /** Asigna un stem SOLO para el tema activo (clave, stem). stem "" = quita el propio. */
    std::function<void (const juce::String& key, const juce::String& stem)> onAssignStemForSong;

    void setShow (const DmxShow* s)
    {
        show = s;
        repaint();
    }

    /** Figura de movimiento activa para los aparatos con motor SIN canales Pan/Tilt
        (spiders/derbies). Auto = la elige la energia del momento. */
    void setMoveFigure (int figIndex)
    {
        moveFigure = (motionfig::Figure) juce::jlimit (0, (int) motionfig::figureNames().size() - 1, figIndex);
    }

    /** Posicion de reproduccion (segundos) para animar el movimiento en tiempo MUSICAL. */
    void setPlayheadSeconds (double s) noexcept { posSeconds = s; }

    /** Activa/desactiva la vista en perspectiva (2.5D). Reversible: la vista
        plana original se conserva intacta y se vuelve a ella al desactivar. */
    void setPerspective (bool shouldUsePerspective)
    {
        perspective = shouldUsePerspective;
        repaint();
    }

    bool isPerspective() const noexcept { return perspective; }

    /** Copia el ultimo frame del motor y repinta. Llamar desde el timer de UI. */
    void refreshFrom (const PlaybackEngine& engine)
    {
        const int nUni = juce::jmax (1, engine.getNumUniverses());
        frame.assign ((size_t) nUni, DmxShow::Universe {});
        for (int u = 0; u < nUni; ++u)
            engine.copyLatestUniverse (u, frame[(size_t) u]);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;

        auto full = getLocalBounds().toFloat();

        // Fondo: degradado de "ambiente de sala" (mas oscuro arriba, suelo tenue).
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff070a10), full.getCentreX(), full.getY(),
                                                 juce::Colour (0xff0d1118), full.getCentreX(), full.getBottom(), false));
        g.fillRect (full);

        if (show == nullptr || ! show->valid || show->fixtures.empty())
        {
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Sin show activo", getLocalBounds(), juce::Justification::centred, false);
            return;
        }

        if (perspective)
        {
            paintPerspective (g);
            return;
        }

        auto area = getLocalBounds().toFloat().reduced (10.0f);
        lastArea = area;

        // Suelo del escenario (linea de horizonte tenue).
        const float floorY = area.getBottom() - 6.0f;
        g.setColour (juce::Colour (P::line).withAlpha (0.5f));
        g.drawLine (area.getX(), floorY, area.getRight(), floorY, 1.0f);

        // Clasifica los equipos en bandas verticales segun su tipo.
        std::vector<int> truss, washRow, barRow, floorRow;
        for (int i = 0; i < (int) show->fixtures.size(); ++i)
        {
            switch (inferKind (show->fixtures[(size_t) i]))
            {
                case Kind::MovingHead:
                case Kind::Spider:   truss.push_back (i);    break;
                case Kind::Wash:     washRow.push_back (i);  break;
                case Kind::Bar:      barRow.push_back (i);   break;
                default:             floorRow.push_back (i); break; // Par, Strobe, Generic
            }
        }

        const float trussY = area.getY() + area.getHeight() * 0.10f;
        const float washY  = area.getY() + area.getHeight() * 0.20f;
        const float barY   = area.getY() + area.getHeight() * 0.45f;

        // Posicion por defecto (en bandas) y, encima, la posicion que el usuario
        // haya fijado arrastrando cada aparato (normalizada al area).
        positions.assign (show->fixtures.size(), {});
        layoutRow (truss,    trussY,         area);
        layoutRow (washRow,  washY,          area);
        layoutRow (barRow,   barY,           area);
        layoutRow (floorRow, floorY - 10.0f, area);
        for (int i = 0; i < (int) show->fixtures.size(); ++i)
        {
            const auto it = customPos.find (fixtureKey (show->fixtures[(size_t) i]));
            if (it != customPos.end())
                positions[(size_t) i] = { area.getX() + it->second.x * area.getWidth(),
                                          area.getY() + it->second.y * area.getHeight() };
        }

        // --- Haces primero (para que los cuerpos queden por encima) ---
        drawRow (truss, [&] (const Fixture& f, juce::Point<float> p)
        {
            const auto st = readState (f);
            if (inferKind (f) == Kind::Spider) drawSpiderBeams (g, p, floorY, st);
            else                               drawHeadBeam   (g, p, floorY, st);
        });

        drawRow (washRow, [&] (const Fixture& f, juce::Point<float> p)
        {
            drawWashBeam (g, p, floorY, readState (f));
        });

        drawRow (floorRow, [&] (const Fixture& f, juce::Point<float> p)
        {
            const auto st = readState (f);
            if (inferKind (f) == Kind::Strobe) drawStrobeGlow (g, p, area, st);
            else                               drawParGlow    (g, p, area.getY(), st);
        });

        // --- Cuerpos de los aparatos encima ---
        drawRow (truss,   [&] (const Fixture& f, juce::Point<float> p) { drawFixtureBody (g, f, p, readState (f)); });
        drawRow (washRow, [&] (const Fixture& f, juce::Point<float> p) { drawFixtureBody (g, f, p, readState (f)); });
        drawRow (barRow,  [&] (const Fixture& f, juce::Point<float> p)
        {
            const int rot = rotationFor (f);
            g.saveState();
            if (rot != 0)
                g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians ((float) rot), p.x, p.y));
            const auto secs = readBarSections (f);
            if (secs.size() >= 3) drawBarPixels (g, p, area, secs);
            else                  drawBar (g, p, area, readState (f));
            g.restoreState();
            drawLabel (g, f, p.translated (0.0f, 16.0f));
            drawStemBadge (g, f, p.translated (0.0f, -16.0f));
        });
        drawRow (floorRow, [&] (const Fixture& f, juce::Point<float> p) { drawFixtureBody (g, f, p, readState (f)); });

        // Resalta el aparato que se esta arrastrando.
        if (draggingFixture >= 0 && draggingFixture < (int) positions.size())
        {
            const auto p = positions[(size_t) draggingFixture];
            g.setColour (juce::Colour (P::accent).withAlpha (0.9f));
            g.drawEllipse (p.x - 16.0f, p.y - 16.0f, 32.0f, 32.0f, 1.5f);
        }

        // Pista de uso (sutil, abajo a la izquierda).
        g.setColour (juce::Colour (P::textDim).withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Arrastra para mover  \xc2\xb7  clic derecho = stem / rotacion  \xc2\xb7  doble clic = auto",
                    area.removeFromBottom (14).withTrimmedLeft (4),
                    juce::Justification::bottomLeft, false);
    }

    //==============================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (fixtureAt (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor
                                                    : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = fixtureAt (e.position);

        // Clic derecho: menu para asignar el stem del equipo.
        if (e.mods.isPopupMenu())
        {
            if (i >= 0 && show != nullptr)
                showStemMenu (i);
            return;
        }

        draggingFixture = i;
        if (draggingFixture >= 0)
        {
            dragOffset = positions[(size_t) draggingFixture] - e.position;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingFixture < 0 || show == nullptr
            || lastArea.getWidth() < 1.0f || lastArea.getHeight() < 1.0f)
            return;

        const auto key = fixtureKey (show->fixtures[(size_t) draggingFixture]);

        if (perspective)
        {
            // Invierte la proyeccion 2.5D: del punto en pantalla saca {fx, d} manteniendo
            // la altura h del equipo (la profundidad cambia al arrastrar en vertical).
            const auto np = e.position + dragOffset;
            const float h = (draggingFixture < (int) persHeights.size())
                              ? persHeights[(size_t) draggingFixture] : 0.0f;
            const float A = persFrontY - h * persHeightScale;
            const float B = 0.60f * h * persHeightScale - (persFrontY - persHorizonY);
            float d = (std::abs (B) > 0.0001f) ? (np.y - A) / B : 0.0f;
            d = juce::jlimit (0.0f, 1.0f, d);
            const float s = 1.0f - 0.60f * d;
            float fx = (s > 0.001f) ? (np.x - persCenterX) / (persHalfW * s) : 0.0f;
            fx = juce::jlimit (-1.0f, 1.0f, fx);
            customPlace[key] = { fx, d };
            repaint();
            return;
        }

        auto np = e.position + dragOffset;
        np.x = juce::jlimit (lastArea.getX(), lastArea.getRight(),  np.x);
        np.y = juce::jlimit (lastArea.getY(), lastArea.getBottom(), np.y);

        customPos[key]
            = { (np.x - lastArea.getX()) / lastArea.getWidth(),
                (np.y - lastArea.getY()) / lastArea.getHeight() };
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingFixture >= 0)
            saveLayout();
        draggingFixture = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = fixtureAt (e.position);
        if (i >= 0 && show != nullptr)
        {
            const auto key = fixtureKey (show->fixtures[(size_t) i]);
            customPos.erase (key);
            customPlace.erase (key);
            saveLayout();
            repaint();
        }
    }

private:
    //==============================================================================
    struct State
    {
        juce::Colour colour { juce::Colours::white };
        float intensity = 0.0f;   // 0..1
        float pan = 0.5f;         // 0..1 (0.5 = centro)
        float tilt = 0.5f;        // 0..1
        float strobe = 0.0f;      // 0..1
    };

    juce::uint8 valueAt (int universe, int address1) const
    {
        if (universe < 0 || universe >= (int) frame.size())
            return 0;
        if (address1 < 1 || address1 > 512)
            return 0;
        return frame[(size_t) universe][(size_t) (address1 - 1)];
    }

    State readState (const Fixture& f) const
    {
        int r = 0, gg = 0, b = 0, w = 0, amb = 0, uv = 0;
        int dim = -1, pan = -1, tilt = -1, strobe = 0;

        for (int ci = 0; ci < f.channelCount(); ++ci)
        {
            const int v = valueAt (f.universe, f.dmxAddressOf (ci));
            switch (f.channels[(size_t) ci].type)
            {
                case ChannelType::Red:    r   = v; break;
                case ChannelType::Green:  gg  = v; break;
                case ChannelType::Blue:   b   = v; break;
                case ChannelType::White:  w   = v; break;
                case ChannelType::Amber:  amb = v; break;
                case ChannelType::UV:     uv  = v; break;
                case ChannelType::Dimmer: dim = v; break;
                case ChannelType::Pan:    pan = v; break;
                case ChannelType::Tilt:   tilt = v; break;
                case ChannelType::Strobe:
                case ChannelType::Shutter: strobe = juce::jmax (strobe, v); break;
                default: break;
            }
        }

        // Color combinado (white/amber/uv suman a los canales RGB).
        float cr = (float) r + w * 0.9f + amb * 0.9f + uv * 0.35f;
        float cg = (float) gg + w * 0.9f + amb * 0.55f;
        float cb = (float) b + w * 0.9f + uv * 0.9f;

        const bool hasColour = (r | gg | b | w | amb | uv) != 0;
        if (! hasColour)
            cr = cg = cb = 255.0f;   // aparatos sin color (strobe/dimmer) = blanco

        const float maxc = juce::jmax (cr, cg, cb, 1.0f);
        State st;
        st.colour = juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, cr / maxc),
                                                 juce::jlimit (0.0f, 1.0f, cg / maxc),
                                                 juce::jlimit (0.0f, 1.0f, cb / maxc), 1.0f);

        const float dimK   = (dim >= 0) ? dim / 255.0f : 1.0f;
        const float colorK = juce::jlimit (0.0f, 1.0f, maxc / 255.0f);
        st.intensity = dimK * (hasColour ? colorK : 1.0f);
        st.pan    = (pan  >= 0) ? pan  / 255.0f : 0.5f;
        st.tilt   = (tilt >= 0) ? tilt / 255.0f : 0.5f;
        st.strobe = strobe / 255.0f;

        // El strobe destellando fuerza un fogonazo brillante este frame.
        if (st.strobe > 0.15f)
            st.intensity = juce::jmax (st.intensity, st.strobe);

        return st;
    }

    /** Lee el color por SECCION de una barra de pixeles (triples R,G,B en orden,
        sumando el blanco correspondiente). Vacio si no es barra de pixeles. */
    std::vector<juce::Colour> readBarSections (const Fixture& f) const
    {
        std::vector<std::array<int, 3>> rgb;
        std::vector<int> white;
        const int n = f.channelCount();
        for (int i = 0; i < n; )
        {
            const auto t = f.channels[(size_t) i].type;
            if (t == ChannelType::Red && i + 2 < n
                && f.channels[(size_t) (i + 1)].type == ChannelType::Green
                && f.channels[(size_t) (i + 2)].type == ChannelType::Blue)
            {
                rgb.push_back ({ i, i + 1, i + 2 });
                i += 3;
            }
            else if (t == ChannelType::White) { white.push_back (i); i += 1; }
            else                              { i += 1; }
        }

        std::vector<juce::Colour> out;
        const int nW = (int) white.size();
        for (int s = 0; s < (int) rgb.size(); ++s)
        {
            const float r = valueAt (f.universe, f.dmxAddressOf (rgb[(size_t) s][0]));
            const float g = valueAt (f.universe, f.dmxAddressOf (rgb[(size_t) s][1]));
            const float b = valueAt (f.universe, f.dmxAddressOf (rgb[(size_t) s][2]));
            float w = 0.0f;
            if (nW == (int) rgb.size())   w = valueAt (f.universe, f.dmxAddressOf (white[(size_t) s]));
            else if (nW > 0)              w = valueAt (f.universe, f.dmxAddressOf (white[(size_t) juce::jmin (nW - 1, s)]));

            out.push_back (juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, (r + w * 0.9f) / 255.0f),
                                                        juce::jlimit (0.0f, 1.0f, (g + w * 0.9f) / 255.0f),
                                                        juce::jlimit (0.0f, 1.0f, (b + w * 0.9f) / 255.0f),
                                                        1.0f));
        }
        return out;
    }

    //==============================================================================
    // VISTA 2.5D (perspectiva). Render autonomo, reutiliza readState/inferKind/etc.

    /** Rellena un haz como un cuadrilatero degradado de `from` (ancho w0) a `to` (ancho w1). */
    static void fillBeamQuad (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to,
                              float w0, float w1, juce::Colour c, float aFrom, float aTo)
    {
        const juce::Point<float> dir = to - from;
        const float len = juce::jmax (1.0f, dir.getDistanceFromOrigin());
        const juce::Point<float> perp { -dir.y / len, dir.x / len };

        juce::Path beam;
        beam.startNewSubPath (from.x - perp.x * w0, from.y - perp.y * w0);
        beam.lineTo          (from.x + perp.x * w0, from.y + perp.y * w0);
        beam.lineTo          (to.x   + perp.x * w1, to.y   + perp.y * w1);
        beam.lineTo          (to.x   - perp.x * w1, to.y   - perp.y * w1);
        beam.closeSubPath();

        g.setGradientFill (juce::ColourGradient (c.withAlpha (aFrom), from.x, from.y,
                                                 c.withAlpha (aTo),   to.x,   to.y, false));
        g.fillPath (beam);
    }

    void paintPerspective (juce::Graphics& g)
    {
        using P = LuxLookAndFeel::Palette;
        auto area = getLocalBounds().toFloat().reduced (10.0f);
        lastArea = area;

        const float centerX     = area.getCentreX();
        const float horizonY    = area.getY() + area.getHeight() * 0.22f;
        const float frontY      = area.getBottom() - 18.0f;
        const float halfWFront  = area.getWidth() * 0.46f;
        const float heightScale = (frontY - horizonY) * 0.62f;

        auto scaleAt = [] (float d) { return 1.0f - 0.60f * juce::jlimit (0.0f, 1.0f, d); };
        auto floorPt = [&] (float fx, float d)
        {
            const float s = scaleAt (d);
            const float y = frontY - juce::jlimit (0.0f, 1.0f, d) * (frontY - horizonY);
            return juce::Point<float> (centerX + fx * halfWFront * s, y);
        };
        auto proj = [&] (float fx, float d, float h)
        {
            const auto fp = floorPt (fx, d);
            return juce::Point<float> (fp.x, fp.y - h * heightScale * scaleAt (d));
        };

        // Reloj de pared para animacion intrinseca (spiders giran, wash respira) en
        // equipos sin Pan/Tilt; el escenario se repinta a ~30Hz durante reproduccion.
        const double animT = juce::Time::getMillisecondCounterHiRes() * 0.001;

        // Suelo en trapecio con degradado de ambiente.
        const auto f00 = floorPt (-1.0f, 0.0f), f10 = floorPt (1.0f, 0.0f);
        const auto f11 = floorPt (1.0f, 1.0f),  f01 = floorPt (-1.0f, 1.0f);
        juce::Path floor;
        floor.startNewSubPath (f00); floor.lineTo (f10); floor.lineTo (f11); floor.lineTo (f01); floor.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff151b26), centerX, frontY,
                                                 juce::Colour (0xff090d14), centerX, horizonY, false));
        g.fillPath (floor);

        // Rejilla del suelo.
        g.setColour (juce::Colour (P::line).withAlpha (0.32f));
        for (int i = 0; i <= 10; ++i)
        {
            const float fx = -1.0f + 2.0f * (float) i / 10.0f;
            const auto a = floorPt (fx, 0.0f), b = floorPt (fx, 1.0f);
            g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
        }
        for (int i = 0; i <= 6; ++i)
        {
            const float d = (float) i / 6.0f;
            const auto a = floorPt (-1.0f, d), b = floorPt (1.0f, d);
            g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
        }

        // Truss (barra de techo) al fondo.
        const auto tl = proj (-1.0f, 0.86f, 1.0f), tr = proj (1.0f, 0.86f, 1.0f);
        g.setColour (juce::Colour (0xff141822)); g.drawLine (tl.x, tl.y, tr.x, tr.y, 4.0f);
        g.setColour (juce::Colour (P::line));    g.drawLine (tl.x, tl.y, tr.x, tr.y, 1.0f);

        // Clasifica los equipos.
        std::vector<int> truss, floorRow, barRow;
        for (int i = 0; i < (int) show->fixtures.size(); ++i)
        {
            switch (inferKind (show->fixtures[(size_t) i]))
            {
                case Kind::MovingHead:
                case Kind::Spider:
                case Kind::Wash:  truss.push_back (i);    break;
                case Kind::Bar:   barRow.push_back (i);   break;
                default:          floorRow.push_back (i); break;
            }
        }

        positions.assign (show->fixtures.size(), {});

        struct Place { float fx, d, h; };
        std::vector<Place> place (show->fixtures.size(), Place { 0.0f, 0.0f, 0.0f });
        auto spread = [] (int k, int n) { return n <= 1 ? 0.0f : juce::jmap ((float) k, 0.0f, (float) (n - 1), -0.82f, 0.82f); };
        for (int k = 0; k < (int) truss.size();    ++k) place[(size_t) truss[(size_t) k]]    = { spread (k, (int) truss.size()),    0.86f, 1.0f };
        for (int k = 0; k < (int) barRow.size();   ++k) place[(size_t) barRow[(size_t) k]]   = { spread (k, (int) barRow.size()),   0.55f, 0.55f };
        for (int k = 0; k < (int) floorRow.size(); ++k) place[(size_t) floorRow[(size_t) k]] = { spread (k, (int) floorRow.size()), 0.14f, 0.0f };

        // Posiciones fijadas por el usuario (arrastre en 2.5D): sobreescriben fx/d.
        for (int i = 0; i < (int) show->fixtures.size(); ++i)
        {
            const auto it = customPlace.find (fixtureKey (show->fixtures[(size_t) i]));
            if (it != customPlace.end())
            {
                place[(size_t) i].fx = juce::jlimit (-1.0f, 1.0f, it->second.x);
                place[(size_t) i].d  = juce::jlimit (0.0f, 1.0f, it->second.y);
            }
        }

        // Guarda los parametros de proyeccion para invertir el arrastre.
        persCenterX = centerX; persHorizonY = horizonY; persFrontY = frontY;
        persHalfW = halfWFront; persHeightScale = heightScale;
        persHeights.assign (show->fixtures.size(), 0.0f);
        for (size_t i = 0; i < place.size(); ++i) persHeights[i] = place[i].h;

        // --- Haces de los equipos colgados (truss) ---
        for (const int idx : truss)
        {
            const auto& f  = show->fixtures[(size_t) idx];
            const auto  st = readState (f);
            const auto  pl = place[(size_t) idx];
            const auto  from = proj (pl.fx, pl.d, pl.h);
            positions[(size_t) idx] = from;
            if (st.intensity <= 0.01f) continue;

            const Kind k = inferKind (f);
            if (k == Kind::Wash)
            {
                // Barrido suave (pan si lo hay + leve oscilacion propia) y cono que se
                // ABRE de forma incremental con la intensidad.
                const float panX = pl.fx + (st.pan - 0.5f) * 0.9f
                                 + 0.12f * (float) std::sin (animT * 0.8 + pl.fx * 2.0f);
                const auto  to   = floorPt (panX, juce::jlimit (0.06f, 0.55f, 0.16f + st.tilt * 0.32f));
                const float spreadW = (45.0f + 95.0f * st.intensity) * scaleAt (pl.d) + 12.0f;
                fillBeamQuad (g, from, to, 10.0f, spreadW, st.colour,
                              0.30f * st.intensity, 0.02f * st.intensity);
            }
            else if (k == Kind::Spider)
            {
                // Spider real: 2 FILAS de 4 (RGBW), cada fila con su MOTOR. El
                // movimiento usa la MISMA gramatica de figuras que las cabezas
                // (motionfig), en tiempo MUSICAL (beats) con frecuencia constante:
                // la energia modula la AMPLITUD, nunca el multiplicador de tiempo
                // (asi se elimina el tiron de animT*velocidad). Las 2 filas van en
                // fases distintas -> se cruzan/intercalan segun la figura.
                const int   perRow = 4;
                const float colSpread = 0.85f;                       // ancho fijo de las 4 columnas
                const double beats = (show != nullptr)
                    ? (posSeconds - show->beatOffset) * show->bpm / 60.0
                    : animT * 2.0; // fallback sin show
                const float energy = st.intensity;
                const int   phrase = (int) std::floor (beats / 8.0);
                const motionfig::Figure fig = (moveFigure == motionfig::Figure::Auto)
                    ? motionfig::figureForEnergy (energy, phrase)
                    : moveFigure;
                const float tiltC = 0.18f + st.tilt * 0.34f;         // centro vertical (tilt si lo hay)
                const float vAmp  = 0.20f;                            // recorrido vertical (la energia ya entra via eval)
                for (int row = 0; row < 2; ++row)
                {
                    const auto  sm    = motionfig::eval (fig, beats, energy, row, 2, 1.0);
                    const float vMove = (sm.pan - 0.5f) * 2.0f * vAmp;     // -vAmp..+vAmp
                    // Las dos filas arrancan de distinta altura del cuerpo (no en linea).
                    const float rowBias = (row == 0 ? -0.06f : 0.06f);
                    const float depth = juce::jlimit (0.05f, 0.95f, pl.d * (0.28f + (float) row * 0.10f)
                                                      + tiltC + vMove + rowBias);
                    for (int c = 0; c < perRow; ++c)
                    {
                        const float t  = (float) c / (float) (perRow - 1);   // 0..1 columna
                        const float tx = pl.fx + (t - 0.5f) * colSpread;     // X FIJA por columna
                        const auto  to = floorPt (tx, depth);
                        g.setColour (st.colour.withAlpha (0.40f * st.intensity));
                        g.drawLine (from.x, from.y, to.x, to.y, 1.4f);
                        g.setColour (st.colour.withAlpha (0.16f * st.intensity));
                        g.fillEllipse (to.x - 3.0f, to.y - 3.0f, 6.0f, 6.0f);
                    }
                }
            }
            else // cabeza movil / beam / spot
            {
                const float panX    = pl.fx + (st.pan - 0.5f) * 1.6f;
                const float targetD = juce::jlimit (0.05f, pl.d, juce::jmap (st.tilt, 0.0f, 1.0f, 0.05f, pl.d));
                const auto  to = floorPt (panX, targetD);
                fillBeamQuad (g, from, to, 3.0f, 12.0f, st.colour, 0.55f * st.intensity, 0.05f * st.intensity);
                const float r = 14.0f * scaleAt (targetD);
                g.setColour (st.colour.withAlpha (0.20f * st.intensity));
                g.fillEllipse (to.x - r, to.y - r * 0.35f, r * 2.0f, r * 0.7f);
            }
        }

        // --- Equipos de suelo (PAR/strobe): resplandor hacia arriba ---
        for (const int idx : floorRow)
        {
            const auto& f  = show->fixtures[(size_t) idx];
            const auto  st = readState (f);
            const auto  pl = place[(size_t) idx];
            const auto  base = proj (pl.fx, pl.d, 0.0f);
            positions[(size_t) idx] = base;

            if (inferKind (f) == Kind::Strobe)
            {
                const float flash = juce::jmax (st.strobe, st.intensity);
                if (flash > 0.05f)
                {
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.5f * flash), base.x, base.y,
                                                             juce::Colours::white.withAlpha (0.0f), base.x, base.y - 160.0f, true));
                    g.fillEllipse (base.x - 90.0f, base.y - 150.0f, 180.0f, 180.0f);
                }
                continue;
            }
            if (st.intensity <= 0.01f) continue;

            const float s   = scaleAt (pl.d);
            const auto  top = juce::Point<float> (base.x, base.y - heightScale * 0.42f * s);
            fillBeamQuad (g, base, top, 6.0f * s, (24.0f + 16.0f * st.intensity) * s, st.colour, 0.30f * st.intensity, 0.0f);
        }

        // --- Cuerpos de los aparatos encima de los haces ---
        auto drawBody25 = [&] (int idx)
        {
            const auto& f  = show->fixtures[(size_t) idx];
            const auto  st = readState (f);
            const auto  pl = place[(size_t) idx];
            const auto  p  = proj (pl.fx, pl.d, pl.h);
            positions[(size_t) idx] = p;
            const float s = scaleAt (pl.d);
            const Kind  k = inferKind (f);

            if (k == Kind::Bar)
            {
                const int rot = rotationFor (f);
                g.saveState();
                if (rot != 0)
                    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians ((float) rot), p.x, p.y));
                const float barW = 150.0f * s;
                const float barH = 9.0f * s;
                juce::Rectangle<float> bar (p.x - barW * 0.5f, p.y - barH * 0.5f, barW, barH);
                g.setColour (juce::Colour (P::control)); g.fillRoundedRectangle (bar, 2.0f);
                const auto secs = readBarSections (f);
                const int  nSeg = juce::jmax (1, (int) secs.size());
                const float segW = (barW - 2.0f) / (float) nSeg;
                for (int i = 0; i < nSeg; ++i)
                {
                    const auto c = secs.empty() ? st.colour : secs[(size_t) i];
                    const float br = juce::jmax (c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
                    juce::Rectangle<float> sg (bar.getX() + 1.0f + i * segW, bar.getY() + 1.0f, segW - 0.8f, barH - 2.0f);
                    g.setColour (c.withAlpha (0.25f + 0.75f * br));
                    g.fillRect (sg);
                }
                g.restoreState();
            }
            else
            {
                const float bodyR = ((k == Kind::MovingHead || k == Kind::Spider) ? 9.0f : 8.0f) * juce::jmax (0.55f, s);
                juce::Rectangle<float> body (p.x - bodyR, p.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

                if (getAssignedStem)
                {
                    const auto stem = getAssignedStem (fixtureKey (f));
                    if (stem.isNotEmpty()) { g.setColour (stemColour (stem).withAlpha (0.95f)); g.drawEllipse (body.expanded (3.0f), 2.0f); }
                }

                g.setColour (juce::Colour (0xff20262f));
                if (k == Kind::Strobe) { g.fillRoundedRectangle (body, 2.0f); g.setColour (juce::Colour (P::line)); g.drawRoundedRectangle (body, 2.0f, 1.0f); }
                else                   { g.fillEllipse (body);                g.setColour (juce::Colour (P::line)); g.drawEllipse (body, 1.0f); }

                auto lens = body.reduced (bodyR * 0.45f);
                g.setColour (st.colour.withAlpha (0.25f + 0.75f * st.intensity));
                g.fillEllipse (lens);
                if (st.intensity > 0.5f)
                {
                    g.setColour (juce::Colours::white.withAlpha ((st.intensity - 0.5f) * 0.9f));
                    g.fillEllipse (lens.reduced (lens.getWidth() * 0.28f));
                }

                drawRotationNotch (g, p, bodyR, rotationFor (f));
            }

            if (s > 0.62f) drawLabel (g, f, p.translated (0.0f, 14.0f * s + 6.0f));
            drawStemBadge (g, f, p.translated (0.0f, -16.0f * s));
        };

        for (const int idx : floorRow) drawBody25 (idx);
        for (const int idx : barRow)   drawBody25 (idx);
        for (const int idx : truss)    drawBody25 (idx);

        // Pista de uso.
        g.setColour (juce::Colour (P::textDim).withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Vista 2.5D  \xc2\xb7  arrastra para mover  \xc2\xb7  clic derecho = stem / rotacion",
                    area.removeFromBottom (14).withTrimmedLeft (4),
                    juce::Justification::bottomLeft, false);
    }

    //==============================================================================
    static Kind inferKind (const Fixture& f)
    {
        const juce::String tag = (f.name + " " + f.model).toLowerCase();

        bool hasPan = false, hasTilt = false, hasStrobe = false, hasRgb = false;
        for (const auto& c : f.channels)
        {
            if (c.type == ChannelType::Pan)  hasPan  = true;
            if (c.type == ChannelType::Tilt) hasTilt = true;
            if (c.type == ChannelType::Strobe || c.type == ChannelType::Shutter) hasStrobe = true;
            if (c.type == ChannelType::Red || c.type == ChannelType::Green || c.type == ChannelType::Blue) hasRgb = true;
        }

        if (tag.contains ("spider") || tag.contains ("arana") || tag.contains ("derby")) return Kind::Spider;
        if (tag.contains ("wash") || tag.contains ("baño") || tag.contains ("bano"))      return Kind::Wash;
        if (tag.contains ("bar") || tag.contains ("barra"))                               return Kind::Bar;
        if (tag.contains ("strob"))                                                       return Kind::Strobe;
        if (tag.contains ("cabeza") || tag.contains ("moving") || tag.contains ("head")
            || tag.contains ("beam") || tag.contains ("spot"))                            return Kind::MovingHead;

        if (hasPan && hasTilt)        return Kind::MovingHead;
        if (hasStrobe && ! hasRgb)    return Kind::Strobe;
        if (hasRgb)                   return Kind::Par;
        return Kind::Generic;
    }

    //==============================================================================
    void layoutRow (const std::vector<int>& idxs, float y, juce::Rectangle<float> area)
    {
        const int n = (int) idxs.size();
        if (n == 0) return;
        const float left = area.getX() + 24.0f;
        const float right = area.getRight() - 24.0f;
        for (int k = 0; k < n; ++k)
        {
            const float t = (n == 1) ? 0.5f : (float) k / (float) (n - 1);
            positions[(size_t) idxs[(size_t) k]] = { left + (right - left) * t, y };
        }
    }

    template <typename Fn>
    void drawRow (const std::vector<int>& idxs, Fn&& fn) const
    {
        for (const int idx : idxs)
            fn (show->fixtures[(size_t) idx], positions[(size_t) idx]);
    }

    int fixtureAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestD = 22.0f;   // radio de captura del raton
        for (int i = 0; i < (int) positions.size(); ++i)
        {
            const float d = positions[(size_t) i].getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    static juce::String fixtureKey (const Fixture& f)
    {
        return f.name + "@" + juce::String (f.universe) + ":" + juce::String (f.startAddress);
    }

    //==============================================================================
    // Stems: color, etiqueta y menu de asignacion.

    static juce::Colour stemColour (const juce::String& stem)
    {
        if (stem == "drums")  return juce::Colour (0xffe0504a);   // rojo
        if (stem == "bass")   return juce::Colour (0xff4f86ff);   // azul
        if (stem == "vocals") return juce::Colour (0xff52d96b);   // verde
        if (stem == "other")  return juce::Colour (0xffffb020);   // ambar
        return juce::Colours::transparentBlack;
    }

    static juce::String stemShort (const juce::String& stem)
    {
        if (stem == "drums")  return "BAT";
        if (stem == "bass")   return "BAJO";
        if (stem == "vocals") return "VOZ";
        if (stem == "other")  return "OTH";
        return {};
    }

    void showStemMenu (int i)
    {
        const auto key = fixtureKey (show->fixtures[(size_t) i]);
        const juce::String cur = getAssignedStem ? getAssignedStem (key) : juce::String();
        const int rot = rotationFor (show->fixtures[(size_t) i]);

        juce::PopupMenu m;
        m.addSectionHeader ("Stem para " + show->fixtures[(size_t) i].name);
        m.addItem (1, "Auto (por tipo)",   true, cur.isEmpty());
        m.addItem (2, "Bateria",            true, cur == "drums");
        m.addItem (3, "Bajo",               true, cur == "bass");
        m.addItem (4, "Voice / Voces",      true, cur == "vocals");
        m.addItem (5, "Other (Guit/Synth)", true, cur == "other");

        // Asignacion SOLO para el tema que suena (sobrescribe la global de esa cancion).
        const bool songAvail = songAssignAvailable && songAssignAvailable();
        if (songAvail)
        {
            const juce::String songCur = getSongStemFor ? getSongStemFor (key) : cur;
            const juce::String songNm  = activeSongName ? activeSongName() : juce::String();
            juce::PopupMenu sub;
            sub.addItem (101, "Igual que todos", true, false);   // quita el propio del tema
            sub.addItem (102, "Bateria",            true, songCur == "drums");
            sub.addItem (103, "Bajo",               true, songCur == "bass");
            sub.addItem (104, "Voice / Voces",      true, songCur == "vocals");
            sub.addItem (105, "Other (Guit/Synth)", true, songCur == "other");
            m.addSeparator();
            m.addSubMenu ("Solo para: " + (songNm.isNotEmpty() ? songNm : juce::String ("este tema")), sub);
        }

        m.addSeparator();
        m.addSectionHeader ("Rotacion");
        m.addItem (10, "Sin rotacion", true, rot == 0);
        m.addItem (11, "Girar 90",     true, rot == 90);
        m.addItem (12, "Girar 180",    true, rot == 180);
        m.addItem (13, "Girar 270",    true, rot == 270);

        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this, key] (int r)
            {
                if (r == 0)
                    return;

                if (r >= 10 && r <= 13)
                {
                    const int deg = r == 11 ? 90 : r == 12 ? 180 : r == 13 ? 270 : 0;
                    if (deg == 0) customRot.erase (key);
                    else          customRot[key] = deg;
                    saveLayout();
                    repaint();
                    return;
                }

                // Asignacion SOLO para el tema activo (submenu 101-105).
                if (r >= 101 && r <= 105)
                {
                    if (! onAssignStemForSong)
                        return;
                    const juce::String stem = r == 102 ? "drums"
                                            : r == 103 ? "bass"
                                            : r == 104 ? "vocals"
                                            : r == 105 ? "other"
                                                       : juce::String();   // r == 101 => igual que todos
                    onAssignStemForSong (key, stem);
                    return;
                }

                if (! onAssignStem)
                    return;
                const juce::String stem = r == 2 ? "drums"
                                        : r == 3 ? "bass"
                                        : r == 4 ? "vocals"
                                        : r == 5 ? "other"
                                                 : juce::String();   // r == 1 => auto
                onAssignStem (key, stem);
            });
    }

    void drawStemBadge (juce::Graphics& g, const Fixture& f, juce::Point<float> centre) const
    {
        if (! getAssignedStem)
            return;
        const juce::String stem = getAssignedStem (fixtureKey (f));
        if (stem.isEmpty())
            return;

        const auto col = stemColour (stem);
        const auto txt = stemShort (stem);

        juce::Rectangle<float> tag (centre.x - 17.0f, centre.y - 7.0f, 34.0f, 13.0f);
        g.setColour (col.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (txt, tag, juce::Justification::centred, false);
    }

    //==============================================================================
    static void drawParGlow (juce::Graphics& g, juce::Point<float> p, float topY, const State& st)
    {
        if (st.intensity <= 0.01f) return;
        const float a = st.intensity;
        const float spread = 26.0f + 18.0f * a;
        const float height = (p.y - topY) * (0.55f + 0.45f * a);

        juce::Path cone;
        cone.startNewSubPath (p.x, p.y);
        cone.lineTo (p.x - spread, p.y - height);
        cone.lineTo (p.x + spread, p.y - height);
        cone.closeSubPath();

        g.setGradientFill (juce::ColourGradient (st.colour.withAlpha (0.30f * a), p.x, p.y,
                                                 st.colour.withAlpha (0.0f), p.x, p.y - height, false));
        g.fillPath (cone);
    }

    static void drawWashBeam (juce::Graphics& g, juce::Point<float> p, float floorY, const State& st)
    {
        if (st.intensity <= 0.01f) return;
        const float a = st.intensity;
        const float spread = 70.0f + 50.0f * a;

        juce::Path cone;
        cone.startNewSubPath (p.x - 8.0f, p.y);
        cone.lineTo (p.x - spread, floorY);
        cone.lineTo (p.x + spread, floorY);
        cone.lineTo (p.x + 8.0f, p.y);
        cone.closeSubPath();

        g.setGradientFill (juce::ColourGradient (st.colour.withAlpha (0.28f * a), p.x, p.y,
                                                 st.colour.withAlpha (0.02f * a), p.x, floorY, false));
        g.fillPath (cone);
    }

    static void drawHeadBeam (juce::Graphics& g, juce::Point<float> p, float floorY, const State& st)
    {
        if (st.intensity <= 0.01f) return;
        const float a = st.intensity;
        const float height = floorY - p.y;

        // Pan desplaza el objetivo en X; Tilt regula el alcance vertical.
        const float panOff = (st.pan - 0.5f) * 2.0f * (height * 0.9f);
        const float reach  = 0.45f + 0.55f * st.tilt;
        const juce::Point<float> tip { p.x + panOff, p.y + height * reach };

        const float w0 = 4.0f;      // ancho en la cabeza
        const float w1 = 16.0f;     // ancho en el destino
        const juce::Point<float> dir = (tip - p);
        const float len = juce::jmax (1.0f, dir.getDistanceFromOrigin());
        const juce::Point<float> perp { -dir.y / len, dir.x / len };

        juce::Path beam;
        beam.startNewSubPath (p.x - perp.x * w0, p.y - perp.y * w0);
        beam.lineTo (p.x + perp.x * w0, p.y + perp.y * w0);
        beam.lineTo (tip.x + perp.x * w1, tip.y + perp.y * w1);
        beam.lineTo (tip.x - perp.x * w1, tip.y - perp.y * w1);
        beam.closeSubPath();

        g.setGradientFill (juce::ColourGradient (st.colour.withAlpha (0.55f * a), p.x, p.y,
                                                 st.colour.withAlpha (0.04f * a), tip.x, tip.y, false));
        g.fillPath (beam);

        // Pequeño charco de luz en el destino.
        g.setColour (st.colour.withAlpha (0.20f * a));
        g.fillEllipse (tip.x - w1, tip.y - 4.0f, w1 * 2.0f, 8.0f);
    }

    static void drawSpiderBeams (juce::Graphics& g, juce::Point<float> p, float floorY, const State& st)
    {
        if (st.intensity <= 0.01f) return;
        const float a = st.intensity;
        const float height = (floorY - p.y) * (0.5f + 0.5f * st.tilt);
        const int   nBeams = 7;
        const float spreadAng = juce::MathConstants<float>::pi * (0.30f + 0.25f * st.pan); // abanico

        for (int i = 0; i < nBeams; ++i)
        {
            const float t = (nBeams == 1) ? 0.5f : (float) i / (float) (nBeams - 1);
            const float ang = juce::MathConstants<float>::halfPi + (t - 0.5f) * spreadAng;
            const juce::Point<float> tip { p.x + std::cos (ang) * height,
                                           p.y + std::sin (ang) * height };
            g.setColour (st.colour.withAlpha (0.45f * a));
            g.drawLine (p.x, p.y, tip.x, tip.y, 1.6f);
            g.setColour (st.colour.withAlpha (0.18f * a));
            g.fillEllipse (tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f);
        }
    }

    static void drawStrobeGlow (juce::Graphics& g, juce::Point<float> p, juce::Rectangle<float> area, const State& st)
    {
        const float flash = juce::jmax (st.strobe, st.intensity);
        if (flash <= 0.05f) return;

        // Fogonazo blanco que cubre buena parte del escenario.
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.45f * flash), p.x, p.y,
                                                 juce::Colours::white.withAlpha (0.0f), p.x, area.getY(), true));
        g.fillEllipse (p.x - 90.0f, p.y - 150.0f, 180.0f, 180.0f);
    }

    static void drawBar (juce::Graphics& g, juce::Point<float> p, juce::Rectangle<float> area, const State& st)
    {
        const float barW = juce::jmin (220.0f, area.getWidth() * 0.5f);
        const float barH = 12.0f;
        juce::Rectangle<float> bar (p.x - barW * 0.5f, p.y - barH * 0.5f, barW, barH);

        g.setColour (juce::Colour (LuxLookAndFeel::Palette::control));
        g.fillRoundedRectangle (bar, 3.0f);

        const int segs = 12;
        const float segW = (barW - 4.0f) / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            juce::Rectangle<float> seg (bar.getX() + 2.0f + i * segW, bar.getY() + 2.0f, segW - 1.5f, barH - 4.0f);
            g.setColour (st.colour.withAlpha (0.35f + 0.65f * st.intensity));
            g.fillRoundedRectangle (seg, 1.5f);
        }

        // Halo de color bajo la barra.
        if (st.intensity > 0.02f)
        {
            g.setGradientFill (juce::ColourGradient (st.colour.withAlpha (0.22f * st.intensity), p.x, bar.getBottom(),
                                                     st.colour.withAlpha (0.0f), p.x, bar.getBottom() + 40.0f, false));
            g.fillRect (bar.getX(), bar.getBottom(), barW, 40.0f);
        }
    }

    /** Barra de pixeles: cada seccion con su propio color (muestra barridos/olas). */
    static void drawBarPixels (juce::Graphics& g, juce::Point<float> p,
                               juce::Rectangle<float> area, const std::vector<juce::Colour>& secs)
    {
        const int   nSeg = (int) secs.size();
        const float barW = juce::jmin (320.0f, area.getWidth() * 0.62f);
        const float barH = 14.0f;
        juce::Rectangle<float> bar (p.x - barW * 0.5f, p.y - barH * 0.5f, barW, barH);

        g.setColour (juce::Colour (LuxLookAndFeel::Palette::control));
        g.fillRoundedRectangle (bar, 3.0f);

        const float segW = (barW - 4.0f) / (float) nSeg;
        float bestA = 0.0f;
        juce::Colour avg = juce::Colours::black;
        float ar = 0, ag = 0, ab = 0;

        for (int i = 0; i < nSeg; ++i)
        {
            const auto c = secs[(size_t) i];
            const float bright = juce::jmax (c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
            juce::Rectangle<float> seg (bar.getX() + 2.0f + i * segW, bar.getY() + 2.0f, segW - 1.0f, barH - 4.0f);
            g.setColour (c.withAlpha (0.20f + 0.80f * bright));
            g.fillRoundedRectangle (seg, 1.2f);
            if (bright > 0.55f)
            {
                g.setColour (juce::Colours::white.withAlpha ((bright - 0.55f) * 0.7f));
                g.fillRoundedRectangle (seg.reduced (segW * 0.30f, barH * 0.22f), 1.0f);
            }
            ar += c.getFloatRed() * bright; ag += c.getFloatGreen() * bright; ab += c.getFloatBlue() * bright;
            bestA = juce::jmax (bestA, bright);
        }

        // Halo de color promedio bajo la barra.
        if (bestA > 0.02f)
        {
            const float inv = 1.0f / juce::jmax (0.001f, ar + ag + ab);
            avg = juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, ar * inv * 1.6f),
                                               juce::jlimit (0.0f, 1.0f, ag * inv * 1.6f),
                                               juce::jlimit (0.0f, 1.0f, ab * inv * 1.6f), 1.0f);
            g.setGradientFill (juce::ColourGradient (avg.withAlpha (0.22f * bestA), p.x, bar.getBottom(),
                                                     avg.withAlpha (0.0f), p.x, bar.getBottom() + 46.0f, false));
            g.fillRect (bar.getX(), bar.getBottom(), barW, 46.0f);
        }
    }

    void drawFixtureBody (juce::Graphics& g, const Fixture& f, juce::Point<float> p, const State& st) const
    {
        using P = LuxLookAndFeel::Palette;
        const Kind k = inferKind (f);

        const float bodyR = (k == Kind::MovingHead || k == Kind::Spider) ? 9.0f : 8.0f;
        juce::Rectangle<float> body (p.x - bodyR, p.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        // Anillo de color del stem asignado (si lo hay).
        if (getAssignedStem)
        {
            const juce::String stem = getAssignedStem (fixtureKey (f));
            if (stem.isNotEmpty())
            {
                g.setColour (stemColour (stem).withAlpha (0.95f));
                g.drawEllipse (body.expanded (3.5f), 2.0f);
            }
        }

        // Carcasa.
        g.setColour (juce::Colour (0xff20262f));
        if (k == Kind::Bar)
            ; // la barra ya se dibuja aparte
        else if (k == Kind::Strobe)
        {
            g.fillRoundedRectangle (body, 2.0f);
            g.setColour (juce::Colour (P::line));
            g.drawRoundedRectangle (body, 2.0f, 1.0f);
        }
        else
        {
            g.fillEllipse (body);
            g.setColour (juce::Colour (P::line));
            g.drawEllipse (body, 1.0f);
        }

        // Lente encendida con el color actual.
        if (k != Kind::Bar)
        {
            auto lens = body.reduced (bodyR * 0.45f);
            g.setColour (st.colour.withAlpha (0.25f + 0.75f * st.intensity));
            g.fillEllipse (lens);
            if (st.intensity > 0.5f)
            {
                g.setColour (juce::Colours::white.withAlpha ((st.intensity - 0.5f) * 0.9f));
                g.fillEllipse (lens.reduced (lens.getWidth() * 0.28f));
            }

            drawRotationNotch (g, p, bodyR, rotationFor (f));
        }

        drawLabel (g, f, p.translated (0.0f, bodyR + 9.0f));
        drawStemBadge (g, f, p.translated (0.0f, -bodyR - 10.0f));
    }

    static void drawLabel (juce::Graphics& g, const Fixture& f, juce::Point<float> centre)
    {
        g.setColour (juce::Colour (LuxLookAndFeel::Palette::textDim));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText (f.name, juce::Rectangle<float> (centre.x - 50.0f, centre.y - 7.0f, 100.0f, 14.0f),
                    juce::Justification::centred, false);
    }

    const DmxShow* show = nullptr;
    std::vector<DmxShow::Universe> frame { DmxShow::Universe {} };

    motionfig::Figure moveFigure = motionfig::Figure::Auto; // figura manual para spiders/derbies
    double            posSeconds = 0.0;                       // posicion de reproduccion (s)

    bool perspective = false;   // vista 2.5D (reversible)

    std::vector<juce::Point<float>>             positions;   // posicion absoluta del ultimo paint (por fixture)
    std::map<juce::String, juce::Point<float>>  customPos;   // posiciones fijadas por el usuario en 2D (normalizadas 0..1)
    std::map<juce::String, juce::Point<float>>  customPlace; // posiciones fijadas en 2.5D ({fx en -1..1, d en 0..1})
    std::map<juce::String, int>                 customRot;   // rotacion del equipo en grados (0/90/180/270)
    juce::Rectangle<float>                      lastArea;
    int                                         draggingFixture = -1;
    juce::Point<float>                          dragOffset;

    // Parametros de la ultima proyeccion 2.5D (para invertir el arrastre pantalla -> escenario).
    float                                       persCenterX = 0.0f, persHorizonY = 0.0f, persFrontY = 0.0f;
    float                                       persHalfW = 1.0f, persHeightScale = 1.0f;
    std::vector<float>                          persHeights;  // altura h usada por cada fixture en 2.5D

    int rotationFor (const Fixture& f) const
    {
        const auto it = customRot.find (fixtureKey (f));
        return it != customRot.end() ? it->second : 0;
    }

    // Pequena flecha en el borde del cuerpo indicando la orientacion (frente) del equipo.
    void drawRotationNotch (juce::Graphics& g, juce::Point<float> centre, float bodyR, int rotDeg) const
    {
        if (rotDeg == 0)
            return;
        const float ang = juce::degreesToRadians ((float) rotDeg) - juce::MathConstants<float>::halfPi; // 0 = arriba
        const float dx = std::cos (ang), dy = std::sin (ang);
        const juce::Point<float> dir (dx, dy);
        const juce::Point<float> perp (-dy, dx);
        const auto tip  = centre + dir * (bodyR + 5.0f);
        const auto baseL = centre + dir * (bodyR + 0.5f) + perp * 3.2f;
        const auto baseR = centre + dir * (bodyR + 0.5f) - perp * 3.2f;
        juce::Path tri;
        tri.startNewSubPath (tip); tri.lineTo (baseL); tri.lineTo (baseR); tri.closeSubPath();
        g.setColour (juce::Colour (LuxLookAndFeel::Palette::accent).withAlpha (0.95f));
        g.fillPath (tri);
    }

    //==============================================================================
    static juce::File layoutFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LuxSync").getChildFile ("stage_layout.xml");
    }

    void loadLayout()
    {
        const auto f = layoutFile();
        if (! f.existsAsFile()) return;
        if (auto xml = juce::XmlDocument::parse (f))
            for (auto* e : xml->getChildIterator())
                if (e->hasTagName ("fixture"))
                {
                    const auto key = e->getStringAttribute ("key");
                    if (e->hasAttribute ("x"))
                        customPos[key] = { (float) e->getDoubleAttribute ("x"),
                                           (float) e->getDoubleAttribute ("y") };
                    if (e->hasAttribute ("fx"))
                        customPlace[key] = { (float) e->getDoubleAttribute ("fx"),
                                             (float) e->getDoubleAttribute ("d") };
                    if (e->hasAttribute ("rot"))
                        customRot[key] = e->getIntAttribute ("rot");
                }
    }

    void saveLayout() const
    {
        juce::XmlElement root ("stageLayout");

        std::set<juce::String> keys;
        for (const auto& kv : customPos)   keys.insert (kv.first);
        for (const auto& kv : customPlace) keys.insert (kv.first);
        for (const auto& kv : customRot)   keys.insert (kv.first);

        for (const auto& key : keys)
        {
            auto* e = root.createNewChildElement ("fixture");
            e->setAttribute ("key", key);

            const auto p2d = customPos.find (key);
            if (p2d != customPos.end())
            {
                e->setAttribute ("x", (double) p2d->second.x);
                e->setAttribute ("y", (double) p2d->second.y);
            }
            const auto p25 = customPlace.find (key);
            if (p25 != customPlace.end())
            {
                e->setAttribute ("fx", (double) p25->second.x);
                e->setAttribute ("d",  (double) p25->second.y);
            }
            const auto rt = customRot.find (key);
            if (rt != customRot.end())
                e->setAttribute ("rot", rt->second);
        }

        const auto f = layoutFile();
        f.getParentDirectory().createDirectory();
        root.writeTo (f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageVisualizer)
};
