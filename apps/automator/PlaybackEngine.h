#pragma once

#include <juce_core/juce_core.h>
#include "../../source/ArtNetSender.h"
#include "../../source/EnttecSender.h"
#include "../../source/SacnSender.h"
#include "DmxShow.h"
#include <atomic>
#include <array>
#include <mutex>

/**
    Motor de reproduccion DMX del AI Automator (Fase 6).

    A frecuencia de refresco (~44 Hz) muestrea el `DmxShow` del tema en curso en
    la posicion de reproduccion actual (en segundos) y:
      - emite cada universo por Art-Net (UDP 6454, reusa source/ArtNetSender),
      - publica el ultimo frame DMX para que la UI lo previsualice.

    El show activo se cambia bajo un lock muy breve (showLock). La UI lee el
    ultimo frame con copyLatestUniverse(), tambien bajo el mismo lock. El timer
    de refresco lo arranca el componente de UI (no este objeto), para no crear
    hilos extra: setActiveShow + tick(seconds) se llaman desde el hilo de mensajes.
*/
class PlaybackEngine
{
public:
    using Universe = DmxShow::Universe;

    PlaybackEngine() = default;

    /** Acceso al emisor Art-Net para configurarlo desde la UI. */
    ArtNetSender& artNet() noexcept { return artnet; }

    /** Acceso al emisor sACN (E1.31) para configurarlo desde la UI. */
    SacnSender& sacn() noexcept { return sacnSender; }

    /** Acceso al emisor Enttec USB Pro para configurarlo desde la UI. */
    EnttecSender& enttec() noexcept { return enttecSender; }

    /** Cambia el show que se esta reproduciendo (nullptr = ninguno). */
    void setActiveShow (const DmxShow* show)
    {
        std::lock_guard<std::mutex> lock (showLock);
        active = show;
        if (active != nullptr)
            active->prepareBuffer (latest);
        else
            latest.assign (1, Universe {});
        for (auto& u : latest) u.fill (0);
    }

    /** Numero de universos del show activo (>=1). */
    int getNumUniverses() const
    {
        std::lock_guard<std::mutex> lock (showLock);
        return (int) latest.size();
    }

    /** Muestrea el show en `seconds` y emite por Art-Net. Hilo de mensajes (timer UI). */
    void tick (double seconds)
    {
        std::lock_guard<std::mutex> lock (showLock);

        // Blackout de emergencia: mantiene todo a 0 ignorando el show.
        if (blackoutLatched.load())
        {
            for (auto& u : latest) u.fill (0);
            emitLatest();
            return;
        }

        if (active == nullptr)
            return;

        active->renderFrame (seconds, latest);

        if (artnet.isEnabled())
            for (int u = 0; u < (int) latest.size(); ++u)
                artnet.sendUniverse (u, latest[(size_t) u].data(), 512);

        if (sacnSender.isEnabled())
            for (int u = 0; u < (int) latest.size(); ++u)
                sacnSender.sendUniverse (u + 1, latest[(size_t) u].data(), 512);

        if (enttecSender.isEnabled())
        {
            const int eu = juce::jlimit (0, juce::jmax (0, (int) latest.size() - 1), enttecSender.getUniverse());
            enttecSender.sendUniverse (latest[(size_t) eu].data(), 512);
        }
    }

    /** Emite un frame de PRUEBA fijo (independiente de la musica) por las salidas
        activas y lo publica para previsualizarlo. Util para verificar patch/cableado. */
    void sendTestFrame (const std::vector<Universe>& frame)
    {
        std::lock_guard<std::mutex> lock (showLock);
        latest = frame;
        if (latest.empty())
            latest.assign (1, Universe {});

        if (artnet.isEnabled())
            for (int u = 0; u < (int) latest.size(); ++u)
                artnet.sendUniverse (u, latest[(size_t) u].data(), 512);

        if (sacnSender.isEnabled())
            for (int u = 0; u < (int) latest.size(); ++u)
                sacnSender.sendUniverse (u + 1, latest[(size_t) u].data(), 512);

        if (enttecSender.isEnabled())
        {
            const int eu = juce::jlimit (0, juce::jmax (0, (int) latest.size() - 1), enttecSender.getUniverse());
            enttecSender.sendUniverse (latest[(size_t) eu].data(), 512);
        }
    }

    /** Pone todos los canales a 0 y, si Art-Net esta activo, emite el blackout. */
    void blackout()
    {
        std::lock_guard<std::mutex> lock (showLock);
        for (auto& u : latest) u.fill (0);
        emitLatest();
    }

    /** Activa/desactiva el blackout de emergencia (latched). Mientras este activo,
        el motor mantiene todos los canales a 0 ignorando el show. */
    void setBlackout (bool shouldBlackout)
    {
        blackoutLatched.store (shouldBlackout);
        if (shouldBlackout)
            blackout();
    }

    bool isBlackout() const noexcept { return blackoutLatched.load(); }

    /** Copia el ultimo frame de un universo para previsualizarlo en la UI. */
    void copyLatestUniverse (int universe, Universe& out) const
    {
        std::lock_guard<std::mutex> lock (showLock);
        if (universe >= 0 && universe < (int) latest.size())
            out = latest[(size_t) universe];
        else
            out.fill (0);
    }

private:
    /** Emite el contenido actual de `latest` por todas las salidas activas.
        Debe llamarse con showLock tomado. */
    void emitLatest()
    {
        if (artnet.isEnabled())
            for (int u = 0; u < (int) latest.size(); ++u)
                artnet.sendUniverse (u, latest[(size_t) u].data(), 512);

        if (sacnSender.isEnabled())
            for (int u = 0; u < (int) latest.size(); ++u)
                sacnSender.sendUniverse (u + 1, latest[(size_t) u].data(), 512);

        if (enttecSender.isEnabled())
        {
            const int eu = juce::jlimit (0, juce::jmax (0, (int) latest.size() - 1), enttecSender.getUniverse());
            enttecSender.sendUniverse (latest[(size_t) eu].data(), 512);
        }
    }

    ArtNetSender artnet;
    SacnSender   sacnSender;
    EnttecSender enttecSender;

    std::atomic<bool> blackoutLatched { false };

    mutable std::mutex     showLock;
    const DmxShow*         active = nullptr;     // propiedad del Track (no se borra aqui)
    std::vector<Universe>  latest { Universe {} }; // ultimo frame renderizado

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackEngine)
};
