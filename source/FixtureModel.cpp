#include "FixtureModel.h"

namespace
{
    struct TypeName { ChannelType type; const char* name; };

    const TypeName kTypeNames[] =
    {
        { ChannelType::Dimmer,   "dimmer"   },
        { ChannelType::Red,      "red"      },
        { ChannelType::Green,    "green"    },
        { ChannelType::Blue,     "blue"     },
        { ChannelType::White,    "white"    },
        { ChannelType::Amber,    "amber"    },
        { ChannelType::UV,       "uv"       },
        { ChannelType::Pan,      "pan"      },
        { ChannelType::PanFine,  "panfine"  },
        { ChannelType::Tilt,     "tilt"     },
        { ChannelType::TiltFine, "tiltfine" },
        { ChannelType::Color,    "color"    },
        { ChannelType::Gobo,     "gobo"     },
        { ChannelType::Strobe,   "strobe"   },
        { ChannelType::Shutter,  "shutter"  },
        { ChannelType::Zoom,     "zoom"     },
        { ChannelType::Focus,    "focus"    },
        { ChannelType::Generic,  "generic"  },
    };
}

juce::String channelTypeToString (ChannelType type)
{
    for (const auto& tn : kTypeNames)
        if (tn.type == type)
            return tn.name;
    return "generic";
}

ChannelType channelTypeFromString (const juce::String& s)
{
    const auto key = s.trim().toLowerCase();
    for (const auto& tn : kTypeNames)
        if (key == tn.name)
            return tn.type;
    return ChannelType::Generic;
}

juce::StringArray allChannelTypeNames()
{
    juce::StringArray names;
    for (const auto& tn : kTypeNames)
        names.add (tn.name);
    return names;
}

namespace
{
    struct EffectName { EffectType type; const char* name; };

    const EffectName kEffectNames[] =
    {
        { EffectType::Sine,     "sine"     },
        { EffectType::Triangle, "triangle" },
        { EffectType::SawUp,    "sawup"    },
        { EffectType::SawDown,  "sawdown"  },
        { EffectType::Square,   "square"   },
        { EffectType::Random,   "random"   },
    };
}

juce::String effectTypeToString (EffectType type)
{
    for (const auto& en : kEffectNames)
        if (en.type == type)
            return en.name;
    return "sine";
}

EffectType effectTypeFromString (const juce::String& s)
{
    const auto key = s.trim().toLowerCase();
    for (const auto& en : kEffectNames)
        if (key == en.name)
            return en.type;
    return EffectType::Sine;
}

juce::StringArray allEffectTypeNames()
{
    juce::StringArray names;
    for (const auto& en : kEffectNames)
        names.add (en.name);
    return names;
}

juce::Colour defaultColourForChannelType (ChannelType type)
{
    switch (type)
    {
        case ChannelType::Red:      return juce::Colour (0xffe23b3b);
        case ChannelType::Green:    return juce::Colour (0xff3bd45f);
        case ChannelType::Blue:     return juce::Colour (0xff4f8cff);
        case ChannelType::White:    return juce::Colour (0xffe8e8e8);
        case ChannelType::Amber:    return juce::Colour (0xffffb020);
        case ChannelType::UV:       return juce::Colour (0xff9b59ff);
        case ChannelType::Dimmer:   return juce::Colour (0xffffd060);
        case ChannelType::Strobe:   return juce::Colour (0xff00d0d0);
        case ChannelType::Shutter:  return juce::Colour (0xff00b0b0);
        case ChannelType::Color:    return juce::Colour (0xffff7ad0);
        case ChannelType::Gobo:     return juce::Colour (0xffb0a060);
        case ChannelType::Zoom:     return juce::Colour (0xff7ad0a0);
        case ChannelType::Focus:    return juce::Colour (0xff7ab0d0);
        case ChannelType::Pan:
        case ChannelType::PanFine:
        case ChannelType::Tilt:
        case ChannelType::TiltFine: return juce::Colour (0xff7aa0c0);
        case ChannelType::Generic:
        default:                    return juce::Colour (0xff4fc3f7);
    }
}

juce::ValueTree Fixture::toValueTree() const
{
    juce::ValueTree v ("Fixture");
    v.setProperty ("name",         name,         nullptr);
    v.setProperty ("manufacturer", manufacturer, nullptr);
    v.setProperty ("model",        model,        nullptr);
    v.setProperty ("universe",     universe,     nullptr);
    v.setProperty ("startAddress", startAddress, nullptr);

    for (const auto& c : channels)
    {
        juce::ValueTree cv ("Channel");
        cv.setProperty ("type",    channelTypeToString (c.type), nullptr);
        cv.setProperty ("label",   c.label,                      nullptr);
        cv.setProperty ("desc",    c.description,                 nullptr);
        cv.setProperty ("default", c.defaultValue,               nullptr);
        cv.setProperty ("colour",  c.colour.toString(),          nullptr);

        for (const auto& rg : c.ranges)
        {
            juce::ValueTree rv ("Range");
            rv.setProperty ("low",  rg.low,         nullptr);
            rv.setProperty ("high", rg.high,        nullptr);
            rv.setProperty ("desc", rg.description, nullptr);
            cv.appendChild (rv, nullptr);
        }

        for (const auto& k : c.keyframes)
        {
            juce::ValueTree kv ("Key");
            kv.setProperty ("beats",   k.timeBeats, nullptr);
            kv.setProperty ("value",   k.value,     nullptr);
            kv.setProperty ("stepped", k.stepped,   nullptr);
            cv.appendChild (kv, nullptr);
        }

        for (const auto& cl : c.clips)
        {
            juce::ValueTree clv ("Clip");
            clv.setProperty ("start",  cl.startBeats,                 nullptr);
            clv.setProperty ("length", cl.lengthBeats,                nullptr);
            clv.setProperty ("type",   effectTypeToString (cl.type),  nullptr);
            clv.setProperty ("period", cl.periodBeats,                nullptr);
            clv.setProperty ("low",    cl.low,                        nullptr);
            clv.setProperty ("high",   cl.high,                       nullptr);
            clv.setProperty ("phase",  cl.phase,                      nullptr);
            cv.appendChild (clv, nullptr);
        }

        v.appendChild (cv, nullptr);
    }

    return v;
}

Fixture Fixture::fromValueTree (const juce::ValueTree& v)
{
    Fixture f;
    f.name         = v.getProperty ("name", "Equipo").toString();
    f.manufacturer = v.getProperty ("manufacturer", "").toString();
    f.model        = v.getProperty ("model", "").toString();
    f.universe     = (int) v.getProperty ("universe", 0);
    f.startAddress = (int) v.getProperty ("startAddress", 1);

    for (int i = 0; i < v.getNumChildren(); ++i)
    {
        const auto cv = v.getChild (i);
        if (! cv.hasType ("Channel"))
            continue;

        ChannelDef c;
        c.type         = channelTypeFromString (cv.getProperty ("type", "generic").toString());
        c.label        = cv.getProperty ("label", "").toString();
        c.description  = cv.getProperty ("desc", "").toString();
        c.defaultValue = (int) cv.getProperty ("default", 0);
        c.colour       = juce::Colour::fromString (cv.getProperty ("colour", "ff4fc3f7").toString());

        for (int j = 0; j < cv.getNumChildren(); ++j)
        {
            const auto rv = cv.getChild (j);
            if (! rv.hasType ("Range"))
                continue;

            ChannelRange rg;
            rg.low         = (int) rv.getProperty ("low", 0);
            rg.high        = (int) rv.getProperty ("high", 255);
            rg.description = rv.getProperty ("desc", "").toString();
            c.ranges.push_back (rg);
        }

        for (int j = 0; j < cv.getNumChildren(); ++j)
        {
            const auto kv = cv.getChild (j);
            if (! kv.hasType ("Key"))
                continue;

            Keyframe k;
            k.timeBeats = (double) kv.getProperty ("beats", 0.0);
            k.value     = (float)  kv.getProperty ("value", 0.0);
            k.stepped   = (bool)   kv.getProperty ("stepped", false);
            c.keyframes.push_back (k);
        }
        sortKeyframes (c.keyframes);

        for (int j = 0; j < cv.getNumChildren(); ++j)
        {
            const auto clv = cv.getChild (j);
            if (! clv.hasType ("Clip"))
                continue;

            EffectClip cl;
            cl.startBeats  = (double) clv.getProperty ("start",  0.0);
            cl.lengthBeats = (double) clv.getProperty ("length", 4.0);
            cl.type        = effectTypeFromString (clv.getProperty ("type", "sine").toString());
            cl.periodBeats = (double) clv.getProperty ("period", 1.0);
            cl.low         = (float)  clv.getProperty ("low",  0.0);
            cl.high        = (float)  clv.getProperty ("high", 255.0);
            cl.phase       = (double) clv.getProperty ("phase", 0.0);
            c.clips.push_back (cl);
        }

        f.channels.push_back (c);
    }

    return f;
}
