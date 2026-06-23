#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Sequence.h"

/**
    Tipo de funcion de un canal DMX de un equipo (segun su manual).
    Cada equipo ocupa N canales consecutivos a partir de su direccion DMX.
*/
enum class ChannelType
{
    Dimmer, Red, Green, Blue, White, Amber, UV,
    Pan, PanFine, Tilt, TiltFine,
    Color, Gobo, Strobe, Shutter, Zoom, Focus,
    Generic
};

juce::String      channelTypeToString   (ChannelType type);
ChannelType       channelTypeFromString (const juce::String& s);
juce::StringArray allChannelTypeNames();

/** Color por defecto sugerido para un tipo de canal (rojo->rojo, dimmer->ambar...).
    Se usa al crear equipos para que las pistas del timeline salgan ya coloreadas. */
juce::Colour      defaultColourForChannelType (ChannelType type);

/** Conversion de tipo de efecto (LFO) <-> texto, para la UI y la serializacion. */
juce::String      effectTypeToString   (EffectType type);
EffectType        effectTypeFromString (const juce::String& s);
juce::StringArray allEffectTypeNames();

/**
    Sub-funcion de un canal en un rango de valores DMX concreto.
    Un mismo canal fisico puede hacer varias cosas segun el valor (p.ej. 0-127
    abierto, 128-255 strobe). Cada rango lleva su propia descripcion.
*/
struct ChannelRange
{
    int          low  = 0;     // 0..255
    int          high = 255;   // 0..255
    juce::String description;  // que hace el canal en ese rango
};

/** Definicion de un canal dentro de un equipo. */
struct ChannelDef
{
    ChannelType  type         = ChannelType::Dimmer;
    juce::String label;             // etiqueta opcional libre
    juce::String description;       // descripcion general del canal (custom)
    int          defaultValue = 0;  // 0..255
    juce::Colour colour { 0xff4fc3f7 }; // color de la pista en el timeline (editable)
    std::vector<ChannelRange> ranges;  // sub-funciones por rango de valor (custom)
    std::vector<Keyframe>   keyframes; // automatizacion en el timeline (ordenada por tiempo)
    std::vector<EffectClip> clips;     // clips de efecto (LFO) encima de los keyframes
};

/**
    Un equipo (fixture) registrado manualmente: nombre, universo, direccion DMX
    de inicio y la lista de canales que ocupa (en orden, segun el manual).
*/
struct Fixture
{
    juce::String            name;
    juce::String            manufacturer;
    juce::String            model;
    int                     universe     = 0;
    int                     startAddress = 1;   // 1..512 (canal del primer slot)
    std::vector<ChannelDef> channels;

    int channelCount() const noexcept            { return (int) channels.size(); }
    int dmxAddressOf (int channelIndex) const     { return startAddress + channelIndex; }
    int lastAddress() const noexcept              { return startAddress + channelCount() - 1; }

    juce::ValueTree toValueTree() const;
    static Fixture  fromValueTree (const juce::ValueTree& v);
};
