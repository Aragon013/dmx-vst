#pragma once

#include "../../source/FixtureModel.h"
#include <vector>

/**
    Construye un rig por defecto para que el AI Automator pueda generar y
    previsualizar un show sin necesidad (aun) de un editor de equipos.

    Layout pensado para la matriz de mapeo del Automator:
      - 4x PAR LED RGBW + Dimmer   -> intensidad (drums) + washes de color (bass)
      - 2x Wash RGBW + Dimmer       -> washes amplios de color de fondo
      - 1x Barra LED RGB            -> washes de color de fondo
      - 2x Cabeza movil (Pan/Tilt/Dimmer/RGB) -> patrones de movimiento (melodia)
      - 2x Spider (Pan/Tilt/Dimmer/RGB)        -> haces en abanico
      - 2x Strobe (Dimmer/Strobe)              -> destellos en transientes

    El nombre/modelo de cada equipo permite que el visualizador y el motor de
    coreografia infieran su tipo. Universo 0; direcciones DMX consecutivas.
*/
namespace DefaultRig
{
    inline ChannelDef makeChannel (ChannelType type, int def = 0)
    {
        ChannelDef c;
        c.type         = type;
        c.defaultValue = def;
        return c;
    }

    inline std::vector<Fixture> build()
    {
        std::vector<Fixture> rig;
        int addr = 1;

        auto place = [&] (Fixture f)
        {
            f.universe     = 0;
            f.startAddress = addr;
            addr += f.channelCount();
            rig.push_back (std::move (f));
        };

        // 4x PAR LED RGBW + Dimmer (5 canales: Dimmer,R,G,B,W)
        for (int i = 0; i < 4; ++i)
        {
            Fixture par;
            par.name  = "PAR " + juce::String (i + 1);
            par.model = "RGBW+Dim";
            par.channels = {
                makeChannel (ChannelType::Dimmer),
                makeChannel (ChannelType::Red),
                makeChannel (ChannelType::Green),
                makeChannel (ChannelType::Blue),
                makeChannel (ChannelType::White)
            };
            place (std::move (par));
        }

        // 2x Wash RGBW + Dimmer (haz ancho)
        for (int i = 0; i < 2; ++i)
        {
            Fixture wash;
            wash.name  = "Wash " + juce::String (i + 1);
            wash.model = "Wash RGBW";
            wash.channels = {
                makeChannel (ChannelType::Dimmer),
                makeChannel (ChannelType::Red),
                makeChannel (ChannelType::Green),
                makeChannel (ChannelType::Blue),
                makeChannel (ChannelType::White)
            };
            place (std::move (wash));
        }

        // 1x Barra LED RGB (3 canales)
        {
            Fixture bar;
            bar.name  = "Barra LED";
            bar.model = "RGB Bar";
            bar.channels = {
                makeChannel (ChannelType::Red),
                makeChannel (ChannelType::Green),
                makeChannel (ChannelType::Blue)
            };
            place (std::move (bar));
        }

        // 2x Cabeza movil (Pan,Tilt,Dimmer,R,G,B)
        for (int i = 0; i < 2; ++i)
        {
            Fixture head;
            head.name  = "Cabeza " + juce::String (i + 1);
            head.model = "Moving Head";
            head.channels = {
                makeChannel (ChannelType::Pan,    128),
                makeChannel (ChannelType::Tilt,   128),
                makeChannel (ChannelType::Dimmer),
                makeChannel (ChannelType::Red),
                makeChannel (ChannelType::Green),
                makeChannel (ChannelType::Blue)
            };
            place (std::move (head));
        }

        // 2x Spider / araña (Pan,Tilt,Dimmer,R,G,B) - se dibuja con haces en abanico
        for (int i = 0; i < 2; ++i)
        {
            Fixture spider;
            spider.name  = "Spider " + juce::String (i + 1);
            spider.model = "Spider";
            spider.channels = {
                makeChannel (ChannelType::Pan,    128),
                makeChannel (ChannelType::Tilt,   128),
                makeChannel (ChannelType::Dimmer),
                makeChannel (ChannelType::Red),
                makeChannel (ChannelType::Green),
                makeChannel (ChannelType::Blue)
            };
            place (std::move (spider));
        }

        // 2x Strobe (Dimmer,Strobe)
        for (int i = 0; i < 2; ++i)
        {
            Fixture strobe;
            strobe.name  = "Strobe " + juce::String (i + 1);
            strobe.model = "Strobe";
            strobe.channels = {
                makeChannel (ChannelType::Dimmer),
                makeChannel (ChannelType::Strobe)
            };
            place (std::move (strobe));
        }

        return rig;
    }
}
