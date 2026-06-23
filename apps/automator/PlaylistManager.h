#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Track.h"
#include "PreprocessWorker.h"
#include "DefaultRig.h"
#include "ChoreographyEngine.h"
#include <vector>
#include <map>
#include <set>

/**
    Gestiona la lista de temas del AI Automator: anadir, quitar, reordenar y
    leer la metadata basica (duracion) de cada archivo de audio.

    Es un ChangeBroadcaster: la UI se suscribe para refrescarse cuando cambia la
    lista o el estado de un tema.

    Fase 2: la lectura de metadata sigue siendo sincrona (rapida, solo cabecera),
    pero al anadir un tema se ENCOLA un pre-proceso en background (PreprocessWorker)
    que comprueba la cache sidecar (.lux) o analiza el audio. Los resultados llegan
    en el hilo de mensajes e identifican el tema por su id estable.
*/
class PlaylistManager : public juce::ChangeBroadcaster
{
public:
    PlaylistManager()
    {
        formatManager.registerBasicFormats();
        rig = DefaultRig::build();
        autoAssignStems();
        loadRecentProjects();
        loadPreferences();

        worker.onStarted = [this] (int id) { onAnalysisStarted (id); };
        worker.onComplete = [this] (int id, TrackAnalysis a, bool ok) { onAnalysisComplete (id, std::move (a), ok); };
    }

    /** Extensiones soportadas para el selector de archivos. */
    juce::String getSupportedWildcard() const
    {
        return formatManager.getWildcardForAllFormats();
    }

    /** True si el tema (por indice) ya tiene stems separados (entrenados). */
    bool trackHasStems (int index) const
    {
        if (index < 0 || index >= (int) tracks.size())
            return false;
        const auto& a = tracks[(size_t) index].analysis;
        return a.valid && ! a.stems.empty();
    }

    /** Fuerza un RE-ENTRENAMIENTO completo del tema: ignora la cache, vuelve a
        separar los stems y re-analiza. Devuelve false si el indice no es valido. */
    bool retrainTrack (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return false;
        auto& t = tracks[(size_t) index];
        if (t.state == Track::State::Error)
            return false;
        t.state = Track::State::Pending;
        worker.enqueue (t.id, t.file, /*wantStems*/ true, /*force*/ true);
        sendChangeMessage();
        return true;
    }

    /** Disponibilidad del backend de stems (HTDemucs/Python). */
    bool isStemSeparationAvailable() const { return worker.isStemSeparationAvailable(); }
    juce::String getStemBackendName() const { return worker.getStemBackendName(); }

    /** Indice del tema cuyo archivo coincide (o -1 si no esta en el motor). */
    int indexForFile (const juce::File& file) const
    {
        for (int i = 0; i < (int) tracks.size(); ++i)
            if (tracks[(size_t) i].file == file)
                return i;
        return -1;
    }

    /** Entrena (analiza + separa stems con IA) un archivo: lo incorpora al motor si no
        estaba y encola su entrenamiento completo. Con force=true ignora la cache y
        reentrena por completo. Devuelve el indice del tema, o -1 si no es valido. */
    int trainFile (const juce::File& file, bool force = false)
    {
        int idx = indexForFile (file);
        if (idx < 0)
            idx = addFile (file);   // lo incorpora (analisis ligero inicial)
        if (idx < 0)
            return -1;

        auto& t = tracks[(size_t) idx];
        if (force)
        {
            retrainTrack (idx);
        }
        else if (t.state != Track::State::Error)
        {
            // "Entrenar" = entrenamiento completo: secciones + separacion de stems.
            t.state = Track::State::Pending;
            worker.enqueue (t.id, t.file, /*wantStems*/ true);
            sendChangeMessage();
        }
        return idx;
    }

    int size() const noexcept { return (int) tracks.size(); }
    bool isEmpty() const noexcept { return tracks.empty(); }

    const Track& getTrack (int index) const { return tracks[(size_t) index]; }

    /** Sustituye las secciones (estructura) de un tema por las editadas a mano,
        las persiste en el sidecar (.lux) y regenera su show para que el cambio
        de energia por seccion se note en las luces. */
    void setSectionsForTrack (int index, std::vector<TrackSection> sections)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;

        auto& t = tracks[(size_t) index];
        if (! t.analysis.valid)
            return;

        t.analysis.sections = std::move (sections);
        SidecarStore::save (t.file, t.analysis);

        // La energia por seccion alimenta la coreografia: regenerar el show.
        if (t.analysis.lengthSeconds > 0.0)
        {
            auto pool = poolFor (t);
            t.show = ChoreographyEngine::generate (t.analysis, rig, makeAssignFnFor (t),
                                                   currentStyle(), preferredFor (t),
                                                   pool.empty() ? nullptr : &pool,
                                                   sectionPalettesFor (t));
        }

        sendChangeMessage();
    }

    /** Rig por defecto (para previsualizar el escenario aunque no haya tema activo). */
    const std::vector<Fixture>& getRig() const noexcept { return rig; }

    //==============================================================================
    // Rig editable + libreria de equipos (predefinidos + custom).

    /** Plantillas de equipos predefinidos disponibles para anadir al rig. */
    static std::vector<Fixture> predefinedTemplates()
    {
        using CT = ChannelType;
        auto ch = [] (CT t, int d = 0, juce::String desc = {})
        {
            ChannelDef c; c.type = t; c.defaultValue = d; c.description = std::move (desc);
            return c;
        };

        std::vector<Fixture> v;

        Fixture par;  par.name = "PAR RGBW";  par.model = "RGBW+Dim";
        par.channels = { ch (CT::Dimmer), ch (CT::Red), ch (CT::Green), ch (CT::Blue), ch (CT::White) };
        v.push_back (par);

        // PAR LED RGB (7 canales): dimmer + RGB + estrobo + macro de color + velocidad.
        Fixture par7; par7.name = "PAR LED 7ch"; par7.model = "RGB 7ch";
        par7.channels = {
            ch (CT::Dimmer,  0, "Atenuador general (0-255)"),
            ch (CT::Red,     0, "Rojo"),
            ch (CT::Green,   0, "Verde"),
            ch (CT::Blue,    0, "Azul"),
            ch (CT::Strobe,  0, "Estrobo (0-255, de lento a rapido)"),
            ch (CT::Color,   0, "Macro de color (mezclas preestablecidas)"),
            ch (CT::Generic, 0, "Velocidad del macro / automatico")
        };
        v.push_back (par7);

        // PAR LED 36 LEDs (9 canales): RGBW + estrobo + 3 funciones (color/programa/velocidad).
        Fixture par36; par36.name = "PAR LED 36"; par36.model = "36 LED 9ch";
        par36.channels = {
            ch (CT::Dimmer,  0, "Atenuador maestro (0-100%)"),
            ch (CT::Red,     0, "Rojo"),
            ch (CT::Green,   0, "Verde"),
            ch (CT::Blue,    0, "Azul"),
            ch (CT::White,   0, "Blanco"),
            ch (CT::Strobe,  0, "Estrobo (0=off, mayor=mas rapido)"),
            ch (CT::Color,   0, "Macros de color"),
            ch (CT::Generic, 0, "Programas automaticos / auto-run"),
            ch (CT::Generic, 0, "Velocidad del programa")
        };
        v.push_back (par36);

        Fixture wash; wash.name = "Wash"; wash.model = "Wash RGBW";
        wash.channels = { ch (CT::Dimmer), ch (CT::Red), ch (CT::Green), ch (CT::Blue), ch (CT::White) };
        v.push_back (wash);

        Fixture bar;  bar.name = "Barra LED"; bar.model = "RGB Bar";
        bar.channels = { ch (CT::Red), ch (CT::Green), ch (CT::Blue) };
        v.push_back (bar);

        Fixture head; head.name = "Cabeza"; head.model = "Moving Head";
        head.channels = { ch (CT::Pan, 128), ch (CT::Tilt, 128), ch (CT::Dimmer), ch (CT::Red), ch (CT::Green), ch (CT::Blue) };
        v.push_back (head);

        Fixture spider; spider.name = "Spider"; spider.model = "Spider";
        spider.channels = { ch (CT::Pan, 128), ch (CT::Tilt, 128), ch (CT::Dimmer), ch (CT::Red), ch (CT::Green), ch (CT::Blue) };
        v.push_back (spider);

        // Spider de 8 luces (2R 2G 2B 2W) con Pan/Tilt - 15 canales.
        Fixture spider8; spider8.name = "Spider 8"; spider8.model = "8 LED 15ch";
        spider8.channels = {
            ch (CT::Pan,    128, "Pan (movimiento horizontal)"),
            ch (CT::Tilt,   128, "Tilt (movimiento vertical)"),
            ch (CT::Dimmer,   0, "Atenuador maestro"),
            ch (CT::Strobe,   0, "Estrobo (0=off, mayor=mas rapido)"),
            ch (CT::Red,      0, "Rojo - bloque 1"),
            ch (CT::Red,      0, "Rojo - bloque 2"),
            ch (CT::Green,    0, "Verde - bloque 1"),
            ch (CT::Green,    0, "Verde - bloque 2"),
            ch (CT::Blue,     0, "Azul - bloque 1"),
            ch (CT::Blue,     0, "Azul - bloque 2"),
            ch (CT::White,    0, "Blanco - bloque 1"),
            ch (CT::White,    0, "Blanco - bloque 2"),
            ch (CT::Generic,  0, "Velocidad de movimiento"),
            ch (CT::Generic,  0, "Programas automaticos / auto-run"),
            ch (CT::Generic,  0, "Reset / funciones")
        };
        v.push_back (spider8);

        // Spider de 8 luces con 2 filas RGBW intercaladas (Motor1/Motor2 + Dimmer +
        // Estrobo + RGBW fila 1 + RGBW fila 2 + efectos + velocidad + reset) - 15 canales.
        Fixture spiderRgbw; spiderRgbw.name = "Spider RGBW"; spiderRgbw.model = "8 LED RGBWx2 15ch";
        spiderRgbw.channels = {
            ch (CT::Pan,    128, "Motor 1 (0-135 grados)"),
            ch (CT::Tilt,   128, "Motor 2 (0-135 grados)"),
            ch (CT::Dimmer,   0, "Atenuacion (0-100%)"),
            ch (CT::Strobe,   0, "Estrobo (0-9 fijo brillante, sube = flash mas rapido)"),
            ch (CT::Red,      0, "Rojo - fila 1"),
            ch (CT::Green,    0, "Verde - fila 1"),
            ch (CT::Blue,     0, "Azul - fila 1"),
            ch (CT::White,    0, "Blanco - fila 1"),
            ch (CT::Red,      0, "Rojo - fila 2"),
            ch (CT::Green,    0, "Verde - fila 2"),
            ch (CT::Blue,     0, "Azul - fila 2"),
            ch (CT::White,    0, "Blanco - fila 2"),
            ch (CT::Generic,  0, "Efectos de luz (macros por rangos)"),
            ch (CT::Generic,  0, "Velocidad de efectos de luz"),
            ch (CT::Generic,  0, "Reset (241-250, no usar en secuencia)")
        };
        v.push_back (spiderRgbw);

        Fixture strobe; strobe.name = "Strobe"; strobe.model = "Strobe";
        strobe.channels = { ch (CT::Dimmer), ch (CT::Strobe) };
        v.push_back (strobe);

        Fixture dim; dim.name = "Dimmer"; dim.model = "1ch";
        dim.channels = { ch (CT::Dimmer) };
        v.push_back (dim);

        return v;
    }

    /** Equipos custom creados por el usuario (plantillas). */
    const std::vector<Fixture>& getCustomFixtures() const noexcept { return customFixtures; }

    /** Construye la barra LED de pixeles (4 tiras x 88 LED: 2 color + 2 blanco), modo 12 canales. */
    static Fixture makeLedBarFixture()
    {
        using CT = ChannelType;
        auto ch = [] (CT t, int d, juce::String desc)
        {
            ChannelDef c; c.type = t; c.defaultValue = d; c.description = std::move (desc);
            return c;
        };

        Fixture bar;
        bar.name  = "Barra LED Pixel";
        bar.model = "4x88 LED  Blanco+Color (12ch)";

        // 1 - Luz afinada (atenuador maestro).
        ChannelDef c1 = ch (CT::Dimmer, 0, "Luz afinada (atenuador maestro)");

        // 2 - Estroboscopico.
        ChannelDef c2 = ch (CT::Strobe, 0, "Estroboscopico");

        // 3 - Barra de direccion: direccion de modo efecto.
        ChannelDef c3 = ch (CT::Generic, 0, "Barra de direccion - direccion de modo efecto");
        c3.ranges = {
            { 0,   127, "Direccion positiva" },
            { 128, 255, "Direccion negativa" }
        };

        // 4 - Barra de color: color de modo efecto (32 valores por color).
        ChannelDef c4 = ch (CT::Color, 0, "Barra de color - color de modo efecto (32 valores por color)");

        // 5 - Modo RGB: a ritmo propio (1 efecto / 5 valores) o audioritmico.
        ChannelDef c5 = ch (CT::Generic, 0, "Modo RGB - ritmo propio (1 efecto/5 valores) o audioritmico");
        c5.ranges = {
            { 0,   249, "RGB a ritmo propio (un efecto por cada 5 valores)" },
            { 250, 255, "RGB audioritmico" }
        };

        // 6 - Velocidad RGB a ritmo propio (automatico), lento a rapido.
        ChannelDef c6 = ch (CT::Generic, 0, "Velocidad RGB a ritmo propio (automatico) - lento a rapido");

        // 7 - Modo blanco: a ritmo propio (1 efecto / 9 valores) o audioritmico.
        ChannelDef c7 = ch (CT::Generic, 0, "Modo blanco - ritmo propio (1 efecto/9 valores) o audioritmico");
        c7.ranges = {
            { 0,   251, "Blanco a ritmo propio (un efecto por cada 9 valores)" },
            { 252, 255, "Blanco audioritmico" }
        };

        // 8 - Velocidad blanco a ritmo propio (audioritmico), lento a rapido.
        ChannelDef c8 = ch (CT::Generic, 0, "Velocidad blanco a ritmo propio (audioritmico) - lento a rapido");

        // 9..12 - Color de fondo con efecto de movimiento automatico.
        ChannelDef c9  = ch (CT::Red,   0, "Dimmer Rojo (color de fondo con efecto de movimiento automatico)");
        ChannelDef c10 = ch (CT::Green, 0, "Dimmer Verde (color de fondo con efecto de movimiento automatico)");
        ChannelDef c11 = ch (CT::Blue,  0, "Dimmer Azul (color de fondo con efecto de movimiento automatico)");
        ChannelDef c12 = ch (CT::White, 0, "Dimmer Blanco (color de fondo con efecto de movimiento automatico)");

        bar.channels = { c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12 };
        return bar;
    }

    /** Barra LED en modo PIXEL de 88 canales: 22 secciones RGB (canales 1-66) +
        22 secciones de blanco (canales 67-88). Cada canal es brillo lineal 0-255
        de un color de su seccion, permitiendo barridos/olas/fades por seccion. */
    static Fixture makeLedBar88Fixture()
    {
        using CT = ChannelType;
        constexpr int kSections = 22;

        Fixture bar;
        bar.name  = "Barra LED Pixel";
        bar.model = "4x88 LED Pixel - 22 secciones RGB+Blanco (88ch)";

        auto rgb = [] (CT t, const juce::String& colour, int section)
        {
            ChannelDef c;
            c.type         = t;
            c.defaultValue = 0;
            c.description  = "Dimmer lineal Seccion " + juce::String (section) + " " + colour;
            c.colour       = defaultColourForChannelType (t);
            return c;
        };

        // Canales 1-66: secciones 1..22, cada una R, G, B.
        for (int s = 1; s <= kSections; ++s)
        {
            bar.channels.push_back (rgb (CT::Red,   "Rojo",  s));
            bar.channels.push_back (rgb (CT::Green, "Verde", s));
            bar.channels.push_back (rgb (CT::Blue,  "Azul",  s));
        }

        // Canales 67-88: secciones 1..22 de blanco (solo intensidad).
        for (int s = 1; s <= kSections; ++s)
            bar.channels.push_back (rgb (CT::White, "Blanco", s));

        return bar;
    }

    /** Siembra (una sola vez) los equipos custom de ejemplo, como la barra LED de pixeles. */
    void seedDefaultsIfNeeded()
    {
        if (seedVersion >= kCurrentSeedVersion)
            return;

        const auto ledBar = makeLedBar88Fixture();

        // Si ya existe una con ese nombre, la actualiza a la definicion canonica;
        // si no, la anade.
        bool replaced = false;
        for (auto& f : customFixtures)
            if (f.name == ledBar.name)
            {
                f = ledBar;
                replaced = true;
                break;
            }

        if (! replaced)
            customFixtures.push_back (ledBar);

        // Sincroniza las copias ya PATCHEADAS en el rig: conserva su universo y
        // direccion (lo que el usuario configuro en el hardware) pero actualiza los
        // canales/modelo a la definicion nueva. Asi el mapa de canales del programa
        // coincide con el del aparato real.
        bool rigChanged = false;
        for (auto& f : rig)
            if (f.name == ledBar.name || f.name.startsWith (ledBar.name))
            {
                const int   savedUniverse = f.universe;
                const int   savedAddress  = f.startAddress;
                const auto  savedName     = f.name;
                f = ledBar;
                f.universe     = savedUniverse;
                f.startAddress = savedAddress;
                f.name         = savedName;
                rigChanged = true;
            }

        seedVersion = kCurrentSeedVersion;

        // Siembra la libreria de coreografias con los estilos de fabrica (una sola vez),
        // cada uno como una coreografia para "Todas" las fixturas.
        if (choreoLibrary.empty())
            for (const auto& preset : ChoreographyEngine::stylePresets())
            {
                ChoreographyEngine::Choreography c;
                c.name      = preset.name;
                c.targetKey = {};   // todas las fixturas
                c.style     = preset;
                choreoLibrary.push_back (c);
            }

        if (rigChanged)
            regenerateShows();
        saveSession();
        sendChangeMessage();
    }

    void addCustomFixture (const Fixture& f)
    {
        customFixtures.push_back (f);
        saveSession();
        sendChangeMessage();
    }

    void updateCustomFixture (int index, const Fixture& f)
    {
        if (index >= 0 && index < (int) customFixtures.size())
        {
            customFixtures[(size_t) index] = f;
            saveSession();
            sendChangeMessage();
        }
    }

    void removeCustomFixture (int index)
    {
        if (index >= 0 && index < (int) customFixtures.size())
        {
            customFixtures.erase (customFixtures.begin() + index);
            saveSession();
            sendChangeMessage();
        }
    }

    /** Plantillas disponibles para anadir al rig: predefinidos + custom. */
    std::vector<Fixture> getFixtureTemplates() const
    {
        std::vector<Fixture> all = predefinedTemplates();
        for (const auto& f : customFixtures)
            all.push_back (f);
        return all;
    }

    /** Equipos del rig asignados a un stem concreto ("drums"/"bass"/"vocals"/"other"). */
    std::vector<int> rigIndicesForStem (const juce::String& stem) const
    {
        std::vector<int> out;
        for (int i = 0; i < (int) rig.size(); ++i)
            if (getStemFor (ChoreographyEngine::fixtureKey (rig[(size_t) i])) == stem)
                out.push_back (i);
        return out;
    }

    /** Anade una copia de la plantilla al rig, le asigna direccion DMX libre y stem. */
    void addFixtureToRig (const Fixture& templ, const juce::String& stem)
    {
        Fixture f = templ;
        f.universe     = 0;
        f.startAddress = nextFreeAddress (f.channelCount());
        f.name         = uniqueRigName (templ.name.isNotEmpty() ? templ.name : "Equipo");
        rig.push_back (f);
        stemAssign[ChoreographyEngine::fixtureKey (f)] = stem;
        regenerateShows();
        saveSession();
        sendChangeMessage();
    }

    /** Quita un equipo del rig por su clave estable. */
    void removeRigFixture (const juce::String& key)
    {
        for (size_t i = 0; i < rig.size(); ++i)
            if (ChoreographyEngine::fixtureKey (rig[i]) == key)
            {
                rig.erase (rig.begin() + (long) i);
                break;
            }
        stemAssign.erase (key);
        regenerateShows();
        saveSession();
        sendChangeMessage();
    }

    /** True si el rango [start, start+count-1] del universo esta libre (sin solapar
        con otro equipo del rig). Con excludeKey no vacio ignora ese equipo (util al
        reubicar el propio). Tambien valida que quepa dentro de 1-512. */
    bool addressRangeFree (int universe, int start, int count, const juce::String& excludeKey = {}) const
    {
        if (start < 1 || count < 1 || start + count - 1 > 512)
            return false;

        const int end = start + count - 1;
        for (const auto& f : rig)
        {
            if (f.universe != universe)
                continue;
            if (excludeKey.isNotEmpty() && ChoreographyEngine::fixtureKey (f) == excludeKey)
                continue;
            if (start <= f.lastAddress() && f.startAddress <= end)   // solapan
                return false;
        }
        return true;
    }

    /** Cambia el universo/direccion DMX de un equipo del rig (por su clave). Devuelve
        false si el rango choca con otro equipo o se sale de 1-512 (no se modifica nada). */
    bool setRigFixtureAddress (const juce::String& key, int newUniverse, int newStart)
    {
        int idx = -1;
        for (int i = 0; i < (int) rig.size(); ++i)
            if (ChoreographyEngine::fixtureKey (rig[(size_t) i]) == key) { idx = i; break; }
        if (idx < 0)
            return false;

        newUniverse = juce::jmax (0, newUniverse);
        auto& f = rig[(size_t) idx];
        if (! addressRangeFree (newUniverse, newStart, f.channelCount(), key))
            return false;

        f.universe     = newUniverse;
        f.startAddress = newStart;
        const auto newKey = ChoreographyEngine::fixtureKey (f);

        if (newKey != key)   // la clave depende de universo/direccion: migra el stem asignado
        {
            const auto it = stemAssign.find (key);
            if (it != stemAssign.end())
            {
                const auto stem = it->second;
                stemAssign.erase (it);
                stemAssign[newKey] = stem;
            }
        }

        regenerateShows();
        saveSession();
        sendChangeMessage();
        return true;
    }

    //==============================================================================
    // Asignacion stem -> equipo (que instrumento controla cada luz en modo Stems IA).

    /** Stem asignado a un equipo por su clave ("" = automatico por tipo). */
    juce::String getStemFor (const juce::String& fixtureKey) const
    {
        const auto it = stemAssign.find (fixtureKey);
        return it != stemAssign.end() ? it->second : juce::String();
    }

    /** Asigna (o limpia, con "") el stem de un equipo y regenera los shows. */
    void setStemFor (const juce::String& fixtureKey, const juce::String& stem)
    {
        if (stem.isEmpty())
            stemAssign.erase (fixtureKey);
        else
            stemAssign[fixtureKey] = stem;

        regenerateShows();
        saveSession();
    }

    /** Regenera la coreografia de todos los temas ya analizados (sin re-analizar).
        Devuelve cuantos temas se regeneraron. */
    int regenerateShows()
    {
        int count = 0;
        for (auto& t : tracks)
            if (t.analysis.valid && t.analysis.lengthSeconds > 0.0)
            {
                auto pool = poolFor (t);
                t.show = ChoreographyEngine::generate (t.analysis, rig, makeAssignFnFor (t),
                                                       currentStyle(), preferredFor (t),
                                                       pool.empty() ? nullptr : &pool,
                                                       sectionPalettesFor (t));
                ++count;
            }
        sendChangeMessage();
        return count;
    }

    /** Regenera la coreografia de UN solo tema (barato). No notifica ni guarda;
        usar para feedback en vivo del tema que suena mientras se arrastra un knob. */
    void regenerateShow (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        auto& t = tracks[(size_t) index];
        if (! (t.analysis.valid && t.analysis.lengthSeconds > 0.0))
            return;
        auto pool = poolFor (t);
        t.show = ChoreographyEngine::generate (t.analysis, rig, makeAssignFnFor (t),
                                               currentStyle(), preferredFor (t),
                                               pool.empty() ? nullptr : &pool,
                                               sectionPalettesFor (t));
    }

    //==============================================================================
    // Fuente de coreografia por tema: Auto (IA) o Manual (piano roll, Fase 4).

    /** Modo de coreografia del tema (Auto si el indice no es valido). */
    Track::ChoreoMode getChoreoMode (int index) const
    {
        if (index < 0 || index >= (int) tracks.size())
            return Track::ChoreoMode::Auto;
        return tracks[(size_t) index].choreoMode;
    }

    bool isManualMode (int index) const { return getChoreoMode (index) == Track::ChoreoMode::Manual; }

    /** True si el tema tiene una coreografia manual editada (valida). */
    bool hasManualShow (int index) const
    {
        return index >= 0 && index < (int) tracks.size()
            && tracks[(size_t) index].manualShow.valid;
    }

    /** Cambia el modo. A Manual solo tiene efecto util si ya hay manualShow;
        usa bakeManualFromAuto / createBlankManual para crearla. */
    void setChoreoMode (int index, Track::ChoreoMode mode)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        tracks[(size_t) index].choreoMode = mode;
        sendChangeMessage();
        saveSession();
    }

    /** Crea la coreografia manual copiando la generada por la IA ("hornear") y
        pone el tema en modo Manual para editarla en el piano roll. */
    void bakeManualFromAuto (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        auto& t = tracks[(size_t) index];
        if (! t.show.valid)
        {
            auto pool = poolFor (t);
            t.show = ChoreographyEngine::generate (t.analysis, rig, makeAssignFnFor (t),
                                                   currentStyle(), preferredFor (t),
                                                   pool.empty() ? nullptr : &pool,
                                                   sectionPalettesFor (t));
        }
        t.manualShow = t.show;        // copia profunda (fixtures con keyframes/clips)
        t.manualShow.valid = true;
        t.choreoMode = Track::ChoreoMode::Manual;
        sendChangeMessage();
        saveSession();
    }

    /** Crea una coreografia manual EN BLANCO (rig sin keyframes) y pone el tema en
        modo Manual. El piano roll parte de cero. */
    void createBlankManual (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        auto& t = tracks[(size_t) index];

        DmxShow s;
        s.valid         = true;
        s.bpm           = t.analysis.valid && t.analysis.estimatedBpm > 0.0 ? t.analysis.estimatedBpm : 120.0;
        s.lengthSeconds = t.lengthSeconds > 0.0 ? t.lengthSeconds : t.analysis.lengthSeconds;
        int maxUni = 0;
        for (const auto& f : rig)
            maxUni = juce::jmax (maxUni, f.universe);
        s.numUniverses  = maxUni + 1;
        s.fixtures      = rig;        // copia del rig actual
        for (auto& f : s.fixtures)    // sin automatizacion: lienzo limpio
            for (auto& c : f.channels)
            {
                c.keyframes.clear();
                c.clips.clear();
            }

        t.manualShow = std::move (s);
        t.choreoMode = Track::ChoreoMode::Manual;
        sendChangeMessage();
        saveSession();
    }

    /** Descarta la coreografia manual y vuelve a modo Auto (IA). */
    void discardManual (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        auto& t = tracks[(size_t) index];
        t.manualShow = DmxShow {};
        t.choreoMode = Track::ChoreoMode::Auto;
        sendChangeMessage();
        saveSession();
    }

    /** Show que debe reproducirse para el tema segun su modo (o nullptr). */
    const DmxShow* activeShowFor (int index) const
    {
        if (index < 0 || index >= (int) tracks.size())
            return nullptr;
        const auto& t = tracks[(size_t) index];
        if (t.choreoMode == Track::ChoreoMode::Manual && t.manualShow.valid)
            return &t.manualShow;
        return t.show.valid ? &t.show : nullptr;
    }

    /** Acceso mutable a la coreografia manual (para que el piano roll la edite). */
    DmxShow* manualShowRef (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return nullptr;
        return &tracks[(size_t) index].manualShow;
    }

    /** Marca el show manual como modificado (notifica a la UI y guarda). */
    void manualShowEdited (int /*index*/)
    {
        sendChangeMessage();
        saveSession();
    }

    //==============================================================================
    // Colores preferidos por cancion (la coreografia solo usa esos colores).

    /** Colores preferidos de un tema (vacio = identidad automatica por chroma). */
    std::vector<juce::Colour> getPreferredColors (int index) const
    {
        std::vector<juce::Colour> out;
        if (index < 0 || index >= (int) tracks.size())
            return out;
        const auto it = preferredColors.find (tracks[(size_t) index].file.getFullPathName());
        if (it != preferredColors.end())
            out = it->second;
        return out;
    }

    /** Fija (o limpia, con vacio) los colores preferidos de un tema y regenera. */
    void setPreferredColors (int index, const std::vector<juce::Colour>& cols)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        const auto key = tracks[(size_t) index].file.getFullPathName();
        if (cols.empty())
            preferredColors.erase (key);
        else
            preferredColors[key] = cols;
        regenerateShows();
        saveSession();
    }

    //==============================================================================
    // Colores por SECCION (verso/coro/subida...). Modo alternativo a los colores
    // globales: cuando esta activo, cada parte de la cancion usa SU propia paleta y
    // los colores globales se ignoran (toggle entre ambos modos).

    /** True si el tema usa colores por seccion en vez de la paleta global. */
    bool isSectionColorMode (int index) const
    {
        if (index < 0 || index >= (int) tracks.size())
            return false;
        const auto it = sectionColorMode.find (tracks[(size_t) index].file.getFullPathName());
        return it != sectionColorMode.end() && it->second;
    }

    /** Activa/desactiva el modo de colores por seccion y regenera. */
    void setSectionColorMode (int index, bool on)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        const auto key = tracks[(size_t) index].file.getFullPathName();
        if (on) sectionColorMode[key] = true;
        else    sectionColorMode.erase (key);
        regenerateShows();
        saveSession();
    }

    /** Colores de una seccion concreta (tipo TrackSection::Type) de un tema. */
    std::vector<juce::Colour> getSectionColors (int index, int sectionType) const
    {
        std::vector<juce::Colour> out;
        if (index < 0 || index >= (int) tracks.size())
            return out;
        const auto it = sectionColors.find (tracks[(size_t) index].file.getFullPathName());
        if (it != sectionColors.end())
        {
            const auto jt = it->second.find (sectionType);
            if (jt != it->second.end())
                out = jt->second;
        }
        return out;
    }

    /** Fija (o limpia, con vacio) los colores de una seccion de un tema y regenera. */
    void setSectionColors (int index, int sectionType, const std::vector<juce::Colour>& cols)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        const auto key = tracks[(size_t) index].file.getFullPathName();
        if (cols.empty())
        {
            auto it = sectionColors.find (key);
            if (it != sectionColors.end())
            {
                it->second.erase (sectionType);
                if (it->second.empty())
                    sectionColors.erase (it);
            }
        }
        else
        {
            sectionColors[key][sectionType] = cols;
        }
        regenerateShows();
        saveSession();
    }

    /** Tipos de seccion presentes en el analisis del tema (orden de aparicion,
        sin repetir). Si no hay analisis, devuelve los tipos habituales. */
    std::vector<int> sectionTypesPresent (int index) const
    {
        std::vector<int> out;
        if (index >= 0 && index < (int) tracks.size())
        {
            for (const auto& s : tracks[(size_t) index].analysis.sections)
                if (std::find (out.begin(), out.end(), s.type) == out.end())
                    out.push_back (s.type);
        }
        if (out.empty())
            out = { TrackSection::Intro, TrackSection::Verse, TrackSection::Chorus,
                    TrackSection::Build, TrackSection::Drop, TrackSection::Break,
                    TrackSection::Outro };
        return out;
    }

    //==============================================================================
    // Estilo de show (dinamica de patron/color): chase, alterno, onda, arcoiris...

    /** Nombres de los estilos disponibles, para poblar un ComboBox. */
    juce::StringArray getStyleNames() const
    {
        juce::StringArray names;
        for (const auto& s : ChoreographyEngine::stylePresets())
            names.add (s.name);
        return names;
    }

    int  getStyleIndex() const noexcept { return styleIndex; }

    /** Cambia el estilo activo y regenera los shows. */
    void setStyleIndex (int idx)
    {
        const int n = (int) ChoreographyEngine::stylePresets().size();
        styleIndex = juce::jlimit (0, juce::jmax (0, n - 1), idx);
        regenerateShows();
        saveSession();
    }

    //==============================================================================
    // Coreografia de MOVIMIENTO de los fixtures con motor (cabezas/spiders).

    /** Nombres de las figuras de movimiento (indice 0 = Auto IA). */
    juce::StringArray getMoveFigureNames() const { return motionfig::figureNames(); }

    int  getMoveFigureIndex() const noexcept { return moveFigureIndex; }

    /** Cambia la figura de movimiento activa y regenera los shows. */
    void setMoveFigureIndex (int idx)
    {
        const int n = (int) motionfig::figureNames().size();
        moveFigureIndex = juce::jlimit (0, juce::jmax (0, n - 1), idx);
        regenerateShows();
        saveSession();
    }

    //==============================================================================
    // Offsets de brillo de la barra de pixeles (suelo de color y de blanco).

    float getColorOffset() const noexcept { return pixelColorOffset; }
    float getWhiteOffset() const noexcept { return pixelWhiteOffset; }

    // Version ligera: solo fija el valor (sin regenerar shows ni guardar a disco).
    // Usar mientras se ARRASTRA el knob; llamar commitOffsets() al soltar.
    void setColorOffsetLive (float v) noexcept { pixelColorOffset = juce::jlimit (0.0f, 1.0f, v); }
    void setWhiteOffsetLive (float v) noexcept { pixelWhiteOffset = juce::jlimit (0.0f, 1.0f, v); }

    // Trabajo pesado: regenera todas las coreografias y guarda la sesion. Llamar
    // una sola vez al terminar de arrastrar el knob (onDragEnd).
    void commitOffsets()
    {
        regenerateShows();
        saveSession();
    }

    void setColorOffset (float v)
    {
        setColorOffsetLive (v);
        commitOffsets();
    }

    void setWhiteOffset (float v)
    {
        setWhiteOffsetLive (v);
        commitOffsets();
    }

    //==============================================================================
    // Libreria de coreografias creadas por el usuario (por fixtura) + seleccion por tema.

    /** Acceso de solo lectura a la libreria de coreografias. */
    const std::vector<ChoreographyEngine::Choreography>& getChoreoLibrary() const noexcept { return choreoLibrary; }

    /** Anade una coreografia nueva a la libreria. Devuelve su indice. */
    int addChoreo (const ChoreographyEngine::Choreography& c)
    {
        choreoLibrary.push_back (c);
        saveSession();
        sendChangeMessage();
        return (int) choreoLibrary.size() - 1;
    }

    /** Reemplaza la coreografia en index (y regenera si esta en uso). */
    void updateChoreo (int index, const ChoreographyEngine::Choreography& c)
    {
        if (index < 0 || index >= (int) choreoLibrary.size())
            return;
        const auto oldName = choreoLibrary[(size_t) index].name;
        choreoLibrary[(size_t) index] = c;

        // Si cambio el nombre, actualiza las selecciones por tema/equipo que lo usaban.
        if (oldName != c.name)
            for (auto& song : songFixtureChoreos)
                for (auto& fx : song.second)
                    for (auto& n : fx.second)
                        if (n == oldName) n = c.name;

        regenerateShows();
        saveSession();
    }

    /** Elimina la coreografia en index y la quita de las selecciones por tema/equipo. */
    void removeChoreo (int index)
    {
        if (index < 0 || index >= (int) choreoLibrary.size())
            return;
        const auto name = choreoLibrary[(size_t) index].name;
        choreoLibrary.erase (choreoLibrary.begin() + index);
        for (auto& song : songFixtureChoreos)
            for (auto& fx : song.second)
                fx.second.removeString (name);
        regenerateShows();
        saveSession();
    }

    /** Indice en la libreria de la coreografia con ese nombre, o -1. */
    int choreoIndexByName (const juce::String& name) const
    {
        for (int i = 0; i < (int) choreoLibrary.size(); ++i)
            if (choreoLibrary[(size_t) i].name == name)
                return i;
        return -1;
    }

    /** Coreografias elegidas para UN equipo (por clave) en un tema (por ruta). */
    juce::StringArray getSongChoreosForFixture (const juce::String& songPath,
                                                const juce::String& fixtureKey) const
    {
        const auto it = songFixtureChoreos.find (songPath);
        if (it == songFixtureChoreos.end())
            return {};
        const auto fit = it->second.find (fixtureKey);
        return fit != it->second.end() ? fit->second : juce::StringArray();
    }

    /** Fija las coreografias de UN equipo en un tema y regenera ese show. */
    void setSongChoreosForFixture (const juce::String& songPath,
                                   const juce::String& fixtureKey,
                                   const juce::StringArray& names)
    {
        auto& byFixture = songFixtureChoreos[songPath];
        if (names.isEmpty())
            byFixture.erase (fixtureKey);
        else
            byFixture[fixtureKey] = names;
        if (byFixture.empty())
            songFixtureChoreos.erase (songPath);
        regenerateShows();
        saveSession();
    }

    /** True si el tema tiene alguna coreografia asignada (en cualquier equipo). */
    bool songHasChoreos (const juce::String& songPath) const
    {
        const auto it = songFixtureChoreos.find (songPath);
        return it != songFixtureChoreos.end() && ! it->second.empty();
    }

    /** Copia TODA la configuracion de coreografias de un tema al resto de rutas
        indicadas (las sobreescribe: elimina y/o agrega para que coincidan). */
    void copySongChoreosToPaths (const juce::String& srcPath,
                                 const juce::StringArray& destPaths)
    {
        const auto it = songFixtureChoreos.find (srcPath);
        const bool srcEmpty = (it == songFixtureChoreos.end() || it->second.empty());

        for (const auto& dest : destPaths)
        {
            if (dest == srcPath)
                continue;
            if (srcEmpty)
                songFixtureChoreos.erase (dest);
            else
                songFixtureChoreos[dest] = it->second;
        }
        regenerateShows();
        saveSession();
    }

    //==============================================================================
    // Configuracion Enttec USB Pro (persistida en la sesion).

    juce::String getEnttecPort() const noexcept { return enttecPort; }
    void setEnttecPort (const juce::String& p) { enttecPort = p.trim(); saveSession(); }

    int getEnttecUniverse() const noexcept { return enttecUniverse; }
    void setEnttecUniverse (int u) { enttecUniverse = juce::jlimit (0, 7, u); saveSession(); }

    bool isEnttecEnabled() const noexcept { return enttecEnabled; }
    void setEnttecEnabled (bool b) { enttecEnabled = b; saveSession(); }

    // Protocolo: 0 = Enttec USB Pro, 1 = Open DMX (FTDI directo).
    int getEnttecProtocol() const noexcept { return enttecProtocol; }
    void setEnttecProtocol (int p) { enttecProtocol = juce::jlimit (0, 1, p); saveSession(); }

    //==============================================================================
    // Configuracion de red Art-Net / sACN (persistida en la sesion).

    bool isArtNetEnabled() const noexcept { return artNetEnabled; }
    void setArtNetEnabled (bool b) { artNetEnabled = b; saveSession(); }

    juce::String getArtNetIp() const noexcept { return artNetIp; }
    void setArtNetIp (const juce::String& ip) { artNetIp = ip.trim(); saveSession(); }

    bool isSacnEnabled() const noexcept { return sacnEnabled; }
    void setSacnEnabled (bool b) { sacnEnabled = b; saveSession(); }

    juce::String getSacnIp() const noexcept { return sacnIp; }
    void setSacnIp (const juce::String& ip) { sacnIp = ip.trim(); saveSession(); }

    juce::String getNetInterface() const noexcept { return netInterface; }
    void setNetInterface (const juce::String& ip) { netInterface = ip.trim(); saveSession(); }

    /** Anade un archivo a la lista y lee su duracion. Devuelve el indice nuevo o -1. */
    int addFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return -1;

        Track t;
        t.id          = nextId++;
        t.file        = file;
        t.displayName = file.getFileNameWithoutExtension();
        readMetadata (t);
        applyRestoredManual (t);
        const int id = t.id;
        const bool ok = (t.state != Track::State::Error);
        tracks.push_back (std::move (t));

        sendChangeMessage();
        if (ok)
            worker.enqueue (id, file, /*wantStems*/ false);   // ligero al anadir; stems al Entrenar
        saveSession();
        return (int) tracks.size() - 1;
    }

    /** Anade varios archivos de golpe (selector multiple). */
    void addFiles (const juce::Array<juce::File>& files)
    {
        for (const auto& f : files)
        {
            if (! f.existsAsFile())
                continue;

            Track t;
            t.id          = nextId++;
            t.file        = f;
            t.displayName = f.getFileNameWithoutExtension();
            readMetadata (t);
            applyRestoredManual (t);
            const int id = t.id;
            const bool ok = (t.state != Track::State::Error);
            tracks.push_back (std::move (t));
            if (ok)
                worker.enqueue (id, f, /*wantStems*/ false);   // ligero al anadir; stems al Entrenar
        }
        sendChangeMessage();
        saveSession();
    }

    void removeTrack (int index)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        tracks.erase (tracks.begin() + index);
        sendChangeMessage();
        saveSession();
    }

    void clear()
    {
        tracks.clear();
        sendChangeMessage();
        saveSession();
    }

    /** Mueve un tema una posicion arriba (-1) o abajo (+1). Devuelve el indice nuevo. */
    int moveTrack (int index, int delta)
    {
        const int target = index + delta;
        if (index < 0 || index >= (int) tracks.size()
            || target < 0 || target >= (int) tracks.size())
            return index;

        std::swap (tracks[(size_t) index], tracks[(size_t) target]);
        sendChangeMessage();
        saveSession();
        return target;
    }

    //==============================================================================
    // Persistencia de la sesion (lista de temas + opciones), en %APPDATA%/LuxSync.

    /** Ruta del archivo de sesion por defecto. */
    static juce::File defaultSessionFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LuxSync")
                   .getChildFile ("automator_session.xml");
    }

    /** Ruta del archivo con la lista de proyectos recientes. */
    static juce::File recentProjectsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LuxSync")
                   .getChildFile ("recent_projects.txt");
    }

    void loadRecentProjects()
    {
        const auto f = recentProjectsFile();
        if (f.existsAsFile())
            recentProjects.restoreFromString (f.loadFileAsString());
        recentProjects.removeNonExistentFiles();
    }

    void saveRecentProjects() const
    {
        const auto f = recentProjectsFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (recentProjects.toString());
    }

    void addRecentProject (const juce::File& file)
    {
        recentProjects.addFile (file);
        recentProjects.setMaxNumberOfItems (12);
        saveRecentProjects();
    }

    /** Ruta del archivo de preferencias de la app. */
    static juce::File preferencesFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LuxSync")
                   .getChildFile ("preferences.xml");
    }

    void loadPreferences()
    {
        const auto f = preferencesFile();
        if (! f.existsAsFile())
            return;
        if (auto xml = juce::XmlDocument::parse (f))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            if (state.isValid())
            {
                prefMusicFolder  = juce::File (state.getProperty ("musicFolder", "").toString());
                prefAutoLoadLast = (bool) state.getProperty ("autoLoadLast", false);
            }
        }
    }

    void savePreferences() const
    {
        juce::ValueTree state ("LuxAutomatorPrefs");
        state.setProperty ("musicFolder", prefMusicFolder.getFullPathName(), nullptr);
        state.setProperty ("autoLoadLast", prefAutoLoadLast, nullptr);
        const auto f = preferencesFile();
        f.getParentDirectory().createDirectory();
        if (auto xml = state.createXml())
            xml->writeTo (f, {});
    }

    /** Guarda la sesion actual (rutas de los temas + opciones) al archivo por defecto. */
    void saveSession() const
    {
        if (restoring || ! sessionLoaded)
            return;
        writeSessionTo (defaultSessionFile());
    }

    /** Construye el ValueTree completo de la sesion/proyecto. */
    juce::ValueTree buildSessionTree() const
    {
        juce::ValueTree state ("LuxAutomatorSession");
        state.setProperty ("style", styleIndex, nullptr);
        state.setProperty ("moveFigure", moveFigureIndex, nullptr);
        state.setProperty ("colorOffset", pixelColorOffset, nullptr);
        state.setProperty ("whiteOffset", pixelWhiteOffset, nullptr);
        state.setProperty ("defaultsSeeded", seedVersion, nullptr);

        juce::ValueTree list ("Tracks");
        for (const auto& t : tracks)
        {
            juce::ValueTree tn ("Track");
            tn.setProperty ("path", t.file.getFullPathName(), nullptr);
            tn.setProperty ("name", t.displayName, nullptr);
            list.appendChild (tn, nullptr);
        }
        state.appendChild (list, nullptr);

        // Asignacion de stems por equipo.
        juce::ValueTree assignTree ("StemAssign");
        for (const auto& kv : stemAssign)
        {
            juce::ValueTree e ("Fixture");
            e.setProperty ("key", kv.first, nullptr);
            e.setProperty ("stem", kv.second, nullptr);
            assignTree.appendChild (e, nullptr);
        }
        state.appendChild (assignTree, nullptr);

        // Asignacion de stems POR TEMA (ruta -> equipos fijados; flag de modo propio).
        juce::ValueTree songStemTree ("SongStemAssign");
        {
            std::set<juce::String> paths;
            for (const auto& kv : songStemAssign) paths.insert (kv.first);
            for (const auto& kv : songStemOverride) if (kv.second) paths.insert (kv.first);
            for (const auto& path : paths)
            {
                juce::ValueTree song ("Song");
                song.setProperty ("path", path, nullptr);
                const auto ovIt = songStemOverride.find (path);
                song.setProperty ("own", (ovIt != songStemOverride.end() && ovIt->second) ? 1 : 0, nullptr);
                const auto mapIt = songStemAssign.find (path);
                if (mapIt != songStemAssign.end())
                    for (const auto& fx : mapIt->second)
                    {
                        juce::ValueTree e ("Fixture");
                        e.setProperty ("key", fx.first, nullptr);
                        e.setProperty ("stem", fx.second, nullptr);
                        song.appendChild (e, nullptr);
                    }
                songStemTree.appendChild (song, nullptr);
            }
        }
        state.appendChild (songStemTree, nullptr);

        // Colores preferidos por cancion (ruta -> lista de colores hex).
        juce::ValueTree prefTree ("Preferred");
        for (const auto& kv : preferredColors)
        {
            if (kv.second.empty())
                continue;
            juce::StringArray hex;
            for (const auto& c : kv.second)
                hex.add (c.toString());
            juce::ValueTree e ("Song");
            e.setProperty ("path", kv.first, nullptr);
            e.setProperty ("colors", hex.joinIntoString (","), nullptr);
            prefTree.appendChild (e, nullptr);
        }
        state.appendChild (prefTree, nullptr);

        // Colores por seccion + modo activo (ruta -> tipo -> colores).
        juce::ValueTree secTree ("SectionColors");
        for (const auto& kv : sectionColors)
        {
            juce::ValueTree song ("Song");
            song.setProperty ("path", kv.first, nullptr);
            const auto modeIt = sectionColorMode.find (kv.first);
            song.setProperty ("mode", (modeIt != sectionColorMode.end() && modeIt->second) ? 1 : 0, nullptr);
            for (const auto& sc : kv.second)
            {
                if (sc.second.empty())
                    continue;
                juce::StringArray hex;
                for (const auto& c : sc.second)
                    hex.add (c.toString());
                juce::ValueTree sect ("Section");
                sect.setProperty ("type", sc.first, nullptr);
                sect.setProperty ("colors", hex.joinIntoString (","), nullptr);
                song.appendChild (sect, nullptr);
            }
            secTree.appendChild (song, nullptr);
        }
        // Temas en modo por seccion pero sin colores guardados aun (preserva el toggle).
        for (const auto& kv : sectionColorMode)
        {
            if (! kv.second || sectionColors.count (kv.first))
                continue;
            juce::ValueTree song ("Song");
            song.setProperty ("path", kv.first, nullptr);
            song.setProperty ("mode", 1, nullptr);
            secTree.appendChild (song, nullptr);
        }
        state.appendChild (secTree, nullptr);

        // Rig editable (equipos en uso).
        juce::ValueTree rigTree ("Rig");
        for (const auto& f : rig)
            rigTree.appendChild (f.toValueTree(), nullptr);
        state.appendChild (rigTree, nullptr);

        // Libreria de equipos custom.
        juce::ValueTree libTree ("Library");
        for (const auto& f : customFixtures)
            libTree.appendChild (f.toValueTree(), nullptr);
        state.appendChild (libTree, nullptr);

        // Libreria de coreografias creadas por el usuario.
        juce::ValueTree choreoTree ("Choreos");
        for (const auto& c : choreoLibrary)
            choreoTree.appendChild (ChoreographyEngine::choreoToTree (c), nullptr);
        state.appendChild (choreoTree, nullptr);

        // Coreografias elegidas por cancion y equipo (ruta -> clave equipo -> nombres).
        juce::ValueTree songChoreoTree ("SongChoreos");
        for (const auto& song : songFixtureChoreos)
        {
            if (song.second.empty())
                continue;
            juce::ValueTree e ("Song");
            e.setProperty ("path", song.first, nullptr);
            for (const auto& fx : song.second)
            {
                if (fx.second.isEmpty())
                    continue;
                juce::ValueTree fe ("Fixture");
                fe.setProperty ("key", fx.first, nullptr);
                fe.setProperty ("names", fx.second.joinIntoString ("\n"), nullptr);
                e.appendChild (fe, nullptr);
            }
            if (e.getNumChildren() > 0)
                songChoreoTree.appendChild (e, nullptr);
        }
        state.appendChild (songChoreoTree, nullptr);

        // Coreografias manuales (piano roll) por cancion + su modo (Auto/Manual).
        juce::ValueTree manualTree ("ManualShows");
        for (const auto& t : tracks)
        {
            if (t.choreoMode != Track::ChoreoMode::Manual && ! t.manualShow.valid)
                continue;
            juce::ValueTree e ("Song");
            e.setProperty ("path", t.file.getFullPathName(), nullptr);
            e.setProperty ("mode", (int) (t.choreoMode == Track::ChoreoMode::Manual ? 1 : 0), nullptr);
            if (t.manualShow.valid)
                e.appendChild (t.manualShow.toValueTree(), nullptr);
            manualTree.appendChild (e, nullptr);
        }
        state.appendChild (manualTree, nullptr);

        state.setProperty ("enttecPort", enttecPort, nullptr);
        state.setProperty ("enttecUniverse", enttecUniverse, nullptr);
        state.setProperty ("enttecEnabled", enttecEnabled, nullptr);
        state.setProperty ("enttecProtocol", enttecProtocol, nullptr);

        state.setProperty ("artNetEnabled", artNetEnabled, nullptr);
        state.setProperty ("artNetIp", artNetIp, nullptr);
        state.setProperty ("sacnEnabled", sacnEnabled, nullptr);
        state.setProperty ("sacnIp", sacnIp, nullptr);
        state.setProperty ("netInterface", netInterface, nullptr);
        return state;
    }

    /** Escribe el estado completo a un archivo (sesion por defecto o proyecto .lux). */
    bool writeSessionTo (const juce::File& file) const
    {
        auto state = buildSessionTree();
        file.getParentDirectory().createDirectory();
        if (auto xml = state.createXml())
            return xml->writeTo (file, {});
        return false;
    }

    /** Carga la sesion guardada (si existe). Re-encola el analisis (usa la cache sidecar). */
    void loadSession()
    {
        const auto f = defaultSessionFile();
        readSessionFrom (f);
        sessionLoaded = true;   // a partir de aqui ya se puede auto-guardar
    }

    /** Lee el estado desde un archivo (sesion por defecto o proyecto .lux). */
    bool readSessionFrom (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return false;

        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid())
            return false;

        applySessionTree (state);
        return true;
    }

    /** Aplica un ValueTree de sesion/proyecto al estado en memoria. */
    void applySessionTree (const juce::ValueTree& state)
    {
        const juce::ScopedValueSetter<bool> guard (restoring, true);

        tracks.clear();
        styleIndex = (int) state.getProperty ("style", 0);
        moveFigureIndex = (int) state.getProperty ("moveFigure", 0);
        pixelColorOffset = (float) (double) state.getProperty ("colorOffset", 0.0);
        pixelWhiteOffset = (float) (double) state.getProperty ("whiteOffset", 0.0);
        seedVersion = (int) state.getProperty ("defaultsSeeded", 0);

        // Recupera la asignacion de stems por equipo.
        stemAssign.clear();
        auto assignTree = state.getChildWithName ("StemAssign");
        for (int i = 0; i < assignTree.getNumChildren(); ++i)
        {
            auto e = assignTree.getChild (i);
            const juce::String key  = e.getProperty ("key").toString();
            const juce::String stem = e.getProperty ("stem").toString();
            if (key.isNotEmpty() && stem.isNotEmpty())
                stemAssign[key] = stem;
        }

        // Recupera la asignacion de stems POR TEMA.
        songStemAssign.clear();
        songStemOverride.clear();
        auto songStemTree = state.getChildWithName ("SongStemAssign");
        for (int i = 0; i < songStemTree.getNumChildren(); ++i)
        {
            auto song = songStemTree.getChild (i);
            const juce::String path = song.getProperty ("path").toString();
            if (path.isEmpty())
                continue;
            if ((int) song.getProperty ("own") != 0)
                songStemOverride[path] = true;
            for (int j = 0; j < song.getNumChildren(); ++j)
            {
                auto e = song.getChild (j);
                const juce::String key  = e.getProperty ("key").toString();
                const juce::String stem = e.getProperty ("stem").toString();
                if (key.isNotEmpty() && stem.isNotEmpty())
                    songStemAssign[path][key] = stem;
            }
        }

        // Recupera los colores preferidos por cancion.
        preferredColors.clear();
        auto prefTree = state.getChildWithName ("Preferred");
        for (int i = 0; i < prefTree.getNumChildren(); ++i)
        {
            auto e = prefTree.getChild (i);
            const juce::String path = e.getProperty ("path").toString();
            const juce::String cols = e.getProperty ("colors").toString();
            if (path.isEmpty() || cols.isEmpty())
                continue;
            juce::StringArray hex;
            hex.addTokens (cols, ",", "");
            std::vector<juce::Colour> list;
            for (const auto& h : hex)
                if (h.trim().isNotEmpty())
                    list.push_back (juce::Colour::fromString (h.trim()));
            if (! list.empty())
                preferredColors[path] = list;
        }

        // Recupera los colores por seccion + el modo activo.
        sectionColors.clear();
        sectionColorMode.clear();
        auto secTree = state.getChildWithName ("SectionColors");
        for (int i = 0; i < secTree.getNumChildren(); ++i)
        {
            auto song = secTree.getChild (i);
            const juce::String path = song.getProperty ("path").toString();
            if (path.isEmpty())
                continue;
            if ((int) song.getProperty ("mode") == 1)
                sectionColorMode[path] = true;
            for (int j = 0; j < song.getNumChildren(); ++j)
            {
                auto sect = song.getChild (j);
                const int type = (int) sect.getProperty ("type");
                const juce::String cols = sect.getProperty ("colors").toString();
                if (cols.isEmpty())
                    continue;
                juce::StringArray hex;
                hex.addTokens (cols, ",", "");
                std::vector<juce::Colour> list;
                for (const auto& h : hex)
                    if (h.trim().isNotEmpty())
                        list.push_back (juce::Colour::fromString (h.trim()));
                if (! list.empty())
                    sectionColors[path][type] = list;
            }
        }

        // Recupera el rig editable (si esta guardado).
        auto rigTree = state.getChildWithName ("Rig");
        if (rigTree.getNumChildren() > 0)
        {
            rig.clear();
            for (int i = 0; i < rigTree.getNumChildren(); ++i)
                rig.push_back (Fixture::fromValueTree (rigTree.getChild (i)));
        }

        // Recupera la libreria de equipos custom.
        customFixtures.clear();
        auto libTree = state.getChildWithName ("Library");
        for (int i = 0; i < libTree.getNumChildren(); ++i)
            customFixtures.push_back (Fixture::fromValueTree (libTree.getChild (i)));

        // Recupera la libreria de coreografias del usuario.
        choreoLibrary.clear();
        auto choreoTree = state.getChildWithName ("Choreos");
        for (int i = 0; i < choreoTree.getNumChildren(); ++i)
            choreoLibrary.push_back (ChoreographyEngine::choreoFromTree (choreoTree.getChild (i)));

        // Recupera las coreografias elegidas por cancion y equipo.
        songFixtureChoreos.clear();
        auto songChoreoTree = state.getChildWithName ("SongChoreos");
        for (int i = 0; i < songChoreoTree.getNumChildren(); ++i)
        {
            auto e = songChoreoTree.getChild (i);
            const juce::String path = e.getProperty ("path").toString();
            if (path.isEmpty())
                continue;

            if (e.getNumChildren() > 0)
            {
                // Formato nuevo: hijos <Fixture key="..." names="a\nb"/>.
                std::map<juce::String, juce::StringArray> byFixture;
                for (int j = 0; j < e.getNumChildren(); ++j)
                {
                    auto fe = e.getChild (j);
                    const juce::String key = fe.getProperty ("key").toString();
                    juce::StringArray arr;
                    arr.addTokens (fe.getProperty ("names").toString(), "\n", "");
                    arr.removeEmptyStrings();
                    if (key.isNotEmpty() && ! arr.isEmpty())
                        byFixture[key] = arr;
                }
                if (! byFixture.empty())
                    songFixtureChoreos[path] = byFixture;
            }
            else
            {
                // Formato antiguo (plano): names="a\nb". Migra a per-equipo segun el
                // target de cada coreografia (vacio = a todos los equipos del rig).
                juce::StringArray arr;
                arr.addTokens (e.getProperty ("names").toString(), "\n", "");
                arr.removeEmptyStrings();
                std::map<juce::String, juce::StringArray> byFixture;
                for (const auto& name : arr)
                {
                    const int ci = choreoIndexByName (name);
                    const juce::String tgt = ci >= 0 ? choreoLibrary[(size_t) ci].targetKey : juce::String();
                    if (tgt.isNotEmpty())
                        byFixture[tgt].add (name);
                    else
                        for (const auto& f : rig)
                            byFixture[ChoreographyEngine::fixtureKey (f)].add (name);
                }
                if (! byFixture.empty())
                    songFixtureChoreos[path] = byFixture;
            }
        }

        // Si no habia asignaciones guardadas, reparte por tipo para llenar los 4 cuadros.
        if (stemAssign.empty())
            autoAssignStems();

        // Recupera las coreografias manuales (piano roll) y su modo, por ruta.
        // Se aplican al crear cada Track (mas abajo, en addFile).
        restoredManual.clear();
        restoredModes.clear();
        auto manualTree = state.getChildWithName ("ManualShows");
        for (int i = 0; i < manualTree.getNumChildren(); ++i)
        {
            auto e = manualTree.getChild (i);
            const juce::String path = e.getProperty ("path").toString();
            if (path.isEmpty())
                continue;
            restoredModes[path] = (int) e.getProperty ("mode", 0);
            auto showTree = e.getChildWithName ("DmxShow");
            if (showTree.isValid())
            {
                auto s = DmxShow::fromValueTree (showTree);
                if (s.valid)
                    restoredManual[path] = std::move (s);
            }
        }

        auto list = state.getChildWithName ("Tracks");
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            const juce::File f (list.getChild (i).getProperty ("path").toString());
            if (f.existsAsFile())
                addFile (f);
        }

        // Recupera configuracion Enttec.
        enttecPort = state.getProperty ("enttecPort", "").toString();
        enttecUniverse = (int) state.getProperty ("enttecUniverse", 0);
        enttecEnabled = (bool) state.getProperty ("enttecEnabled", false);
        enttecProtocol = (int) state.getProperty ("enttecProtocol", 0);

        // Recupera configuracion de red Art-Net / sACN.
        artNetEnabled = (bool) state.getProperty ("artNetEnabled", false);
        artNetIp = state.getProperty ("artNetIp", "127.0.0.1").toString();
        sacnEnabled = (bool) state.getProperty ("sacnEnabled", false);
        sacnIp = state.getProperty ("sacnIp", "").toString();
        netInterface = state.getProperty ("netInterface", "").toString();
    }

    //==============================================================================
    // Proyectos .lux: empaquetan toda la sesion (temas, rig, shows manuales,
    // estilos, colores y coreografias) en un archivo movible.

    static juce::String projectExtension() { return ".lux"; }

    /** Archivo del proyecto abierto actualmente (vacio = sesion sin nombre). */
    juce::File getCurrentProjectFile() const { return currentProjectFile; }

    /** Guarda el proyecto en el archivo indicado (extension .lux). */
    bool saveProjectToFile (const juce::File& file)
    {
        auto target = file;
        if (! target.hasFileExtension ("lux"))
            target = target.withFileExtension ("lux");

        if (! writeSessionTo (target))
            return false;

        currentProjectFile = target;
        addRecentProject (target);
        return true;
    }

    /** Abre un proyecto .lux y reemplaza la sesion en memoria. */
    bool openProjectFile (const juce::File& file)
    {
        if (! readSessionFrom (file))
            return false;

        currentProjectFile = file;
        addRecentProject (file);
        saveSession();          // refleja el proyecto abierto en la autosesion
        sendChangeMessage();
        return true;
    }

    /** Empieza un proyecto nuevo: vacia los temas y sus shows, conserva el rig. */
    void newProject()
    {
        {
            const juce::ScopedValueSetter<bool> guard (restoring, true);
            tracks.clear();
            stemAssign.clear();
            preferredColors.clear();
            sectionColors.clear();
            sectionColorMode.clear();
            songFixtureChoreos.clear();
            restoredManual.clear();
            restoredModes.clear();
            autoAssignStems();
        }
        currentProjectFile = juce::File();
        saveSession();
        sendChangeMessage();
    }

    /** Lista de proyectos recientes (mas reciente primero). */
    juce::Array<juce::File> getRecentProjects() const
    {
        juce::Array<juce::File> out;
        for (int i = 0; i < recentProjects.getNumFiles(); ++i)
            out.add (recentProjects.getFile (i));
        return out;
    }

    void clearRecentProjects()
    {
        recentProjects.clear();
        saveRecentProjects();
    }

    //==============================================================================
    // Preferencias de la aplicacion (carpeta de musica por defecto, autocarga).

    juce::File getMusicFolder() const { return prefMusicFolder; }
    void setMusicFolder (const juce::File& folder)
    {
        prefMusicFolder = folder;
        savePreferences();
    }

    bool getAutoLoadLastProject() const { return prefAutoLoadLast; }
    void setAutoLoadLastProject (bool on)
    {
        prefAutoLoadLast = on;
        savePreferences();
    }

private:
    //==============================================================================
    // Helpers de rig editable / asignacion automatica de stems.

    /** Stem por defecto segun el tipo de equipo (para llenar los 4 cuadros al inicio). */
    static juce::String autoStemFor (const Fixture& f)
    {
        const juce::String tag = (f.name + " " + f.model).toLowerCase();
        bool hasPan = false, hasStrobe = false;
        for (const auto& c : f.channels)
        {
            if (c.type == ChannelType::Pan)    hasPan = true;
            if (c.type == ChannelType::Strobe) hasStrobe = true;
        }

        if (tag.contains ("bar") || tag.contains ("barra"))                       return "bass";
        if (tag.contains ("wash"))                                                return "vocals";
        if (tag.contains ("cabeza") || tag.contains ("head")
            || tag.contains ("moving") || tag.contains ("spider") || hasPan)      return "other";
        if (hasStrobe)                                                            return "drums";
        return "drums";
    }

    /** Rellena stemAssign para todos los equipos del rig que no tengan asignacion. */
    void autoAssignStems()
    {
        for (const auto& f : rig)
        {
            const auto key = ChoreographyEngine::fixtureKey (f);
            if (stemAssign.find (key) == stemAssign.end())
                stemAssign[key] = autoStemFor (f);
        }
    }

    /** Primera direccion DMX libre en el universo 0 para un equipo de N canales. */
    int nextFreeAddress (int channelCount) const
    {
        int next = 1;
        for (const auto& f : rig)
            if (f.universe == 0)
                next = juce::jmax (next, f.lastAddress() + 1);
        return juce::jlimit (1, juce::jmax (1, 513 - juce::jmax (1, channelCount)), next);
    }

    /** Devuelve un nombre que no choque con otro equipo del rig. */
    juce::String uniqueRigName (const juce::String& base) const
    {
        auto exists = [this] (const juce::String& nm)
        {
            for (const auto& f : rig)
                if (f.name == nm)
                    return true;
            return false;
        };

        if (! exists (base))
            return base;

        for (int n = 2; n < 1000; ++n)
        {
            const juce::String nm = base + " " + juce::String (n);
            if (! exists (nm))
                return nm;
        }
        return base;
    }

    void readMetadata (Track& t)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (t.file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
        {
            t.state = Track::State::Error;
            t.lengthSeconds = 0.0;
            return;
        }

        t.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
        t.state = Track::State::Pending;   // playable, pero aun sin analizar
    }

    /** Aplica la coreografia manual / modo restaurados de la sesion a un Track recien creado. */
    void applyRestoredManual (Track& t)
    {
        const auto path = t.file.getFullPathName();

        auto mit = restoredModes.find (path);
        if (mit != restoredModes.end())
            t.choreoMode = mit->second == 1 ? Track::ChoreoMode::Manual : Track::ChoreoMode::Auto;

        auto sit = restoredManual.find (path);
        if (sit != restoredManual.end())
            t.manualShow = sit->second;
    }

    int indexForId (int id) const
    {
        for (int i = 0; i < (int) tracks.size(); ++i)
            if (tracks[(size_t) i].id == id)
                return i;
        return -1;
    }

    void onAnalysisStarted (int id)
    {
        const int i = indexForId (id);
        if (i < 0) return;
        if (tracks[(size_t) i].state != Track::State::Error)
        {
            tracks[(size_t) i].state = Track::State::Analyzing;
            sendChangeMessage();
        }
    }

    void onAnalysisComplete (int id, TrackAnalysis analysis, bool ok)
    {
        const int i = indexForId (id);
        if (i < 0) return;

        auto& t = tracks[(size_t) i];
        if (ok)
        {
            t.analysis = std::move (analysis);
            if (t.lengthSeconds <= 0.0 && t.analysis.lengthSeconds > 0.0)
                t.lengthSeconds = t.analysis.lengthSeconds;

            // Generar la coreografia DMX a partir del analisis + el rig.
            auto pool = poolFor (t);
            t.show = ChoreographyEngine::generate (t.analysis, rig, makeAssignFnFor (t),
                                                   currentStyle(), preferredFor (t),
                                                   pool.empty() ? nullptr : &pool,
                                                   sectionPalettesFor (t));
        }
        // Analizado o no, el tema sigue siendo reproducible.
        t.state = Track::State::Ready;
        sendChangeMessage();
    }

    juce::AudioFormatManager formatManager;
    std::vector<Track>       tracks;
    std::vector<Fixture>     rig;
    std::vector<Fixture>     customFixtures;   // plantillas de equipos custom del usuario
    PreprocessWorker         worker;
    int                      nextId = 1;
    bool                     restoring = false;   // suprime auto-guardado mientras se carga la sesion
    bool                     sessionLoaded = false;   // hasta cargar la sesion, no se guarda (evita pisar el archivo con el estado por defecto)

    std::map<juce::String, juce::String> stemAssign;   // clave de equipo -> stem ("" = auto)
    // Asignacion de stems POR TEMA: ruta -> (clave de equipo -> stem) y ruta -> usar la propia.
    std::map<juce::String, std::map<juce::String, juce::String>> songStemAssign;
    std::map<juce::String, bool> songStemOverride;
    std::map<juce::String, std::vector<juce::Colour>> preferredColors;  // ruta de tema -> colores preferidos
    // ruta de tema -> (tipo de seccion -> colores) y ruta -> usar colores por seccion.
    std::map<juce::String, std::map<int, std::vector<juce::Colour>>> sectionColors;
    std::map<juce::String, bool> sectionColorMode;
    // Coreografias manuales restauradas de la sesion, por ruta (se aplican al crear el Track).
    std::map<juce::String, DmxShow> restoredManual;
    std::map<juce::String, int>     restoredModes;   // ruta -> 0=Auto, 1=Manual
    std::vector<ChoreographyEngine::Choreography> choreoLibrary;  // coreografias creadas por el usuario
    // ruta de tema -> (clave de equipo -> nombres de coreografias elegidas para ESE equipo).
    std::map<juce::String, std::map<juce::String, juce::StringArray>> songFixtureChoreos;
    int                                  styleIndex = 0;   // indice del estilo de show activo
    int                                  moveFigureIndex = 0; // figura de movimiento (0 = Auto IA)
    float                                pixelColorOffset = 0.0f;  // suelo RGB barra pixel (0..1)
    float                                pixelWhiteOffset = 0.0f;  // suelo blanco barra pixel (0..1)
    int                                  seedVersion = 0;  // version de los custom de ejemplo ya sembrados
    static constexpr int                 kCurrentSeedVersion = 6;
    juce::String                         enttecPort;       // puerto COM (p.ej. "COM3")
    int                                  enttecUniverse = 0;   // universo DMX (0..7)
    bool                                 enttecEnabled = false;   // estado guardado
    int                                  enttecProtocol = 0;   // 0=USB Pro, 1=Open DMX

    bool                                 artNetEnabled = false;        // envio Art-Net activo
    juce::String                         artNetIp = "127.0.0.1";       // IP unicast (vacio = broadcast)
    bool                                 sacnEnabled = false;          // envio sACN activo
    juce::String                         sacnIp;                       // IP unicast (vacio = multicast)
    juce::String                         netInterface;                 // IP local de la NIC de salida (vacio = sistema)
    juce::File                           currentProjectFile;   // .lux abierto (vacio = sin nombre)
    juce::RecentlyOpenedFilesList        recentProjects;       // proyectos recientes
    juce::File                           prefMusicFolder;      // carpeta de musica por defecto
    bool                                 prefAutoLoadLast = false;  // autocargar ultimo proyecto al abrir

    /** Estilo de show seleccionado actualmente. */
    ChoreographyEngine::ShowStyle currentStyle() const
    {
        const auto presets = ChoreographyEngine::stylePresets();
        if (presets.empty())
            return {};
        auto s = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, styleIndex)];
        s.colorOffset = pixelColorOffset;
        s.whiteOffset = pixelWhiteOffset;
        s.moveFigure  = (motionfig::Figure) juce::jlimit (0, (int) motionfig::figureNames().size() - 1, moveFigureIndex);
        return s;
    }

    /** Construye la funcion que el motor usa para saber el stem de cada equipo. */
    ChoreographyEngine::StemAssignFn makeAssignFn() const
    {
        return [this] (const Fixture& f) { return getStemFor (ChoreographyEngine::fixtureKey (f)); };
    }

    /** Funcion de asignacion de stems para UN tema concreto. Si el tema tiene
        "asignacion propia" activada, usa su mapa (clave de equipo -> stem) con
        fallback al global para los equipos no fijados por el tema; si no, usa el
        global. Asi un tema puede acentuar con otro instrumento sin tocar el resto. */
    ChoreographyEngine::StemAssignFn makeAssignFnFor (const Track& t) const
    {
        const auto path = t.file.getFullPathName();
        const auto ovIt = songStemOverride.find (path);
        if (ovIt == songStemOverride.end() || ! ovIt->second)
            return makeAssignFn();

        std::map<juce::String, juce::String> m;
        const auto mapIt = songStemAssign.find (path);
        if (mapIt != songStemAssign.end())
            m = mapIt->second;

        return [this, m] (const Fixture& f) -> juce::String
        {
            const auto key = ChoreographyEngine::fixtureKey (f);
            const auto it = m.find (key);
            if (it != m.end())
                return it->second;          // fijado por el tema
            return getStemFor (key);        // resto: asignacion global
        };
    }

    //==============================================================================
    // Asignacion de stems POR TEMA (sobrescribe la global solo para ese tema).
public:

    /** True si el tema usa su propia asignacion de stems (no la global). */
    bool isSongStemOverride (int index) const
    {
        if (index < 0 || index >= (int) tracks.size())
            return false;
        const auto it = songStemOverride.find (tracks[(size_t) index].file.getFullPathName());
        return it != songStemOverride.end() && it->second;
    }

    /** Activa/desactiva la asignacion propia del tema y regenera su show. */
    void setSongStemOverride (int index, bool on)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        const auto path = tracks[(size_t) index].file.getFullPathName();
        if (on) songStemOverride[path] = true;
        else    songStemOverride.erase (path);
        regenerateShow (index);
        sendChangeMessage();
        saveSession();
    }

    /** Stem que el tema asigna a un equipo: el propio si esta fijado, si no el global. */
    juce::String getSongStemFor (int index, const juce::String& key) const
    {
        if (index >= 0 && index < (int) tracks.size())
        {
            const auto mapIt = songStemAssign.find (tracks[(size_t) index].file.getFullPathName());
            if (mapIt != songStemAssign.end())
            {
                const auto it = mapIt->second.find (key);
                if (it != mapIt->second.end())
                    return it->second;
            }
        }
        return getStemFor (key);
    }

    /** Fija (o limpia con "") el stem que un tema asigna a un equipo y regenera su show. */
    void setSongStemFor (int index, const juce::String& key, const juce::String& stem)
    {
        if (index < 0 || index >= (int) tracks.size())
            return;
        const auto path = tracks[(size_t) index].file.getFullPathName();
        songStemOverride[path] = true;                 // fijar implica activar el modo propio
        if (stem.isEmpty()) songStemAssign[path].erase (key);
        else                songStemAssign[path][key] = stem;
        regenerateShow (index);
        sendChangeMessage();
        saveSession();
    }

private:
    /** Colores preferidos de un tema convertidos al formato del motor (vacio = auto).
        En modo "colores por seccion" devuelve vacio: la paleta global se ignora. */
    std::vector<ChoreographyEngine::RGB> preferredFor (const Track& t) const
    {
        std::vector<ChoreographyEngine::RGB> out;
        const auto path = t.file.getFullPathName();
        const auto modeIt = sectionColorMode.find (path);
        if (modeIt != sectionColorMode.end() && modeIt->second)
            return out;   // modo por seccion: no usar la paleta global
        const auto it = preferredColors.find (path);
        if (it != preferredColors.end())
            for (const auto& c : it->second)
                out.push_back ({ c.getRed(), c.getGreen(), c.getBlue() });
        return out;
    }

    /** Paletas por seccion de un tema (tipo -> colores) para el motor. Vacio si el
        tema no esta en modo por seccion (entonces se usa la paleta global). */
    ChoreographyEngine::SectionPaletteMap sectionPalettesFor (const Track& t) const
    {
        ChoreographyEngine::SectionPaletteMap out;
        const auto path = t.file.getFullPathName();
        const auto modeIt = sectionColorMode.find (path);
        if (modeIt == sectionColorMode.end() || ! modeIt->second)
            return out;
        const auto it = sectionColors.find (path);
        if (it == sectionColors.end())
            return out;
        for (const auto& kv : it->second)
        {
            std::vector<ChoreographyEngine::RGB> pal;
            for (const auto& c : kv.second)
                pal.push_back ({ c.getRed(), c.getGreen(), c.getBlue() });
            if (! pal.empty())
                out[kv.first] = pal;
        }
        return out;
    }

    /** Pool de coreografias seleccionadas para un tema, una entrada por (equipo ->
        coreografia). Cada coreografia se RE-APUNTA a la clave del equipo al que se
        asigno, de modo que el motor la aplica solo a ese equipo. Inyecta los offsets
        de brillo de la barra de pixeles en cada estilo. */
    std::vector<ChoreographyEngine::Choreography> poolFor (const Track& t) const
    {
        std::vector<ChoreographyEngine::Choreography> pool;
        const auto it = songFixtureChoreos.find (t.file.getFullPathName());
        if (it == songFixtureChoreos.end())
            return pool;

        for (const auto& fx : it->second)            // fx.first = clave de equipo
            for (const auto& name : fx.second)       // fx.second = nombres asignados
                for (const auto& c : choreoLibrary)
                    if (c.name == name)
                    {
                        auto cc = c;
                        cc.targetKey         = fx.first;   // re-apunta a ESTE equipo
                        cc.style.colorOffset = pixelColorOffset;
                        cc.style.whiteOffset = pixelWhiteOffset;
                        pool.push_back (cc);
                        break;
                    }
        return pool;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistManager)
};
