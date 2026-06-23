#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

/**
    Senales de analisis publicadas por un "bus" (una pista/stem).

    Las escribe una instancia Connector desde el hilo de audio (atomicos, sin lock)
    y las lee la instancia Main. lastUpdateMs permite saber si el bus sigue "vivo".
*/
struct BusSignals
{
    std::atomic<float>  level          { 0.0f };
    std::atomic<float>  bass           { 0.0f };
    std::atomic<float>  transient      { 0.0f };
    std::atomic<int>    transientCount { 0 };
    std::atomic<double> lastUpdateMs   { 0.0 };
};

/**
    Hub compartido entre todas las instancias del plugin que viven en el mismo
    proceso (el del DAW). Permite que varios Connectors (uno por stem) publiquen
    sus senales bajo un nombre, y que el Main las consuma para mapear pistas->luces.

    El registro guarda weak_ptr: cuando un Connector se destruye, su bus desaparece
    automaticamente (salvo que otra instancia lo comparta por el mismo nombre).
*/
class SharedHub
{
public:
    static SharedHub& getInstance()
    {
        static SharedHub instance;
        return instance;
    }

    /** Obtiene (o crea) el bus con ese nombre. El llamador debe MANTENER vivo el
        shared_ptr devuelto mientras quiera publicar/aparecer en la lista. */
    std::shared_ptr<BusSignals> getOrCreateBus (const juce::String& name)
    {
        const juce::ScopedLock sl (lock);

        auto it = registry.find (name);
        if (it != registry.end())
            if (auto existing = it->second.lock())
                return existing;

        auto created = std::make_shared<BusSignals>();
        registry[name] = created;
        return created;
    }

    /** Devuelve el bus con ese nombre si esta vivo, o nullptr. */
    std::shared_ptr<BusSignals> findBus (const juce::String& name)
    {
        const juce::ScopedLock sl (lock);
        auto it = registry.find (name);
        if (it != registry.end())
            return it->second.lock();
        return nullptr;
    }

    /** Nombres de buses actualmente vivos (purga los expirados de paso). */
    juce::StringArray getBusNames()
    {
        const juce::ScopedLock sl (lock);

        juce::StringArray names;
        for (auto it = registry.begin(); it != registry.end(); )
        {
            if (it->second.expired())
            {
                it = registry.erase (it);
            }
            else
            {
                names.add (it->first);
                ++it;
            }
        }
        return names;
    }

private:
    SharedHub() = default;

    juce::CriticalSection lock;
    std::map<juce::String, std::weak_ptr<BusSignals>> registry;

    JUCE_DECLARE_NON_COPYABLE (SharedHub)
};
