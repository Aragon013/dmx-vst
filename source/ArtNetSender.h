#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <array>

/**
    Emisor Art-Net (DMX sobre red, UDP puerto 6454).

    Convierte el contenido de un universo DMX (hasta 512 canales) en un paquete
    ArtDmx y lo envia por UDP, en broadcast a la subred o en unicast a una IP
    concreta (la del nodo/visualizador Art-Net).

    Pensado para llamarse a ~30-44 Hz (refresco DMX) desde el hilo que rellena
    el buffer DMX. La configuracion (activado, IP, puerto, broadcast) se puede
    cambiar desde otro hilo: los flags son atomicos y la IP se protege con un
    lock muy breve.

    No requiere drivers ni hardware especial: cualquier visualizador Art-Net en
    la misma red (o en localhost) recibe los datos.
*/
class ArtNetSender
{
public:
    ArtNetSender() = default;
    ~ArtNetSender() = default;

    /** Activa o desactiva el envio. Al activar se crea el socket. */
    void setEnabled (bool shouldBeEnabled);
    bool isEnabled() const noexcept { return enabled.load(); }

    /** true = broadcast a la subred (no necesita IP); false = unicast a targetIp. */
    void setBroadcast (bool shouldBroadcast) noexcept { broadcast.store (shouldBroadcast); }
    bool isBroadcast() const noexcept { return broadcast.load(); }

    /** IP de destino en modo unicast (p.ej. "192.168.1.50" o "127.0.0.1"). */
    void setTargetIp (const juce::String& ip);
    juce::String getTargetIp() const;

    /** IP local de la interfaz de red por la que salir (broadcast/unicast).
        Vacia = la que elija el sistema. Cambiarla recrea el socket. */
    void setLocalInterface (const juce::String& ip);
    juce::String getLocalInterface() const;

    /** Puerto UDP (estandar Art-Net = 6454). */
    void setPort (int p) noexcept { port.store (juce::jlimit (1, 65535, p)); }
    int  getPort() const noexcept { return port.load(); }

    /** Reinicia los contadores de secuencia. */
    void reset() noexcept;

    /** Envia un universo DMX. universe = Port-Address de 15 bits (0..32767).
        numChannels: 1..512 (se redondea a par, minimo 2, como pide la spec). */
    void sendUniverse (int universe, const juce::uint8* data, int numChannels);

private:
    bool ensureSocket();

    std::atomic<bool> enabled   { false };
    std::atomic<bool> broadcast { true };
    std::atomic<int>  port      { 6454 };

    mutable juce::CriticalSection ipLock;
    juce::String targetIp { "255.255.255.255" };
    juce::String localInterface;
    std::unique_ptr<juce::DatagramSocket> socket;          // protegido por socketLock
    juce::CriticalSection socketLock;
    // Contador de secuencia por universo (indexado por universe % 512).
    // Art-Net usa 1..255 (0 = secuencia deshabilitada).
    std::array<juce::uint8, 512> sequence {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArtNetSender)
};
