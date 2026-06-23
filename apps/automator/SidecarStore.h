#pragma once

#include <juce_core/juce_core.h>
#include "TrackAnalysis.h"

/**
    Lee y escribe el sidecar de cache (.lux) junto al archivo de audio, para
    evitar re-analizar un tema que ya fue procesado.

    El sidecar es el mismo nombre del audio con extension ".lux" (XML del
    ValueTree de TrackAnalysis). Se considera valido solo si coincide la firma
    (tamano + fecha) del archivo de origen, asi un re-encode invalida la cache.
*/
class SidecarStore
{
public:
    static juce::File sidecarFor (const juce::File& audioFile)
    {
        return audioFile.withFileExtension ("lux");
    }

    /** Devuelve el analisis cacheado si existe y coincide con el audio actual. */
    static TrackAnalysis load (const juce::File& audioFile)
    {
        const auto side = sidecarFor (audioFile);
        if (! side.existsAsFile())
            return {};

        std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (side));
        if (xml == nullptr)
            return {};

        auto analysis = TrackAnalysis::fromValueTree (juce::ValueTree::fromXml (*xml));

        // Validar firma del archivo de origen.
        if (analysis.valid
            && (analysis.sourceSize != audioFile.getSize()
                || analysis.sourceModifiedMs != audioFile.getLastModificationTime().toMilliseconds()))
            return {};   // cache obsoleta

        return analysis;
    }

    /** Escribe el sidecar. Devuelve true si OK. */
    static bool save (const juce::File& audioFile, const TrackAnalysis& analysis)
    {
        const auto side = sidecarFor (audioFile);
        if (auto xml = std::unique_ptr<juce::XmlElement> (analysis.toValueTree().createXml()))
            return xml->writeTo (side);
        return false;
    }
};
