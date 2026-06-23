#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <array>

/**
    Emisor sACN (Streaming ACN, estandar E1.31) para DMX sobre red.

    Envia los 512 canales de un universo en un paquete E1.31 por UDP al puerto
    5568, normalmente en MULTICAST a la direccion 239.255.<uniHi>.<uniLo>
    (calculada a partir del numero de universo, 1..63999). Tambien admite
    unicast a una IP concreta.

    Ventajas frente a Art-Net: multicast (cada nodo se suscribe solo a sus
    universos), prioridad por universo y campo de nombre de fuente.

    Pensado para llamarse a ~30-44 Hz desde el hilo que rellena el buffer DMX.
    La configuracion es atomica / protegida por locks breves, para poder
    cambiarla desde la UI sin parar el envio.
*/
class SacnSender
{
public:
    SacnSender();
    ~SacnSender() = default;

    /** Activa o desactiva el envio. Al activar se crea el socket. */
    void setEnabled (bool shouldBeEnabled);
    bool isEnabled() const noexcept { return enabled.load(); }

    /** true = multicast (239.255.x.x, no necesita IP); false = unicast a targetIp. */
    void setMulticast (bool shouldMulticast) noexcept { multicast.store (shouldMulticast); }
    bool isMulticast() const noexcept { return multicast.load(); }

    /** IP de destino en modo unicast. */
    void setTargetIp (const juce::String& ip);
    juce::String getTargetIp() const;

    /** IP local de la interfaz de red por la que salir (multicast/unicast).
        Vacia = la que elija el sistema. Cambiarla recrea el socket. */
    void setLocalInterface (const juce::String& ip);
    juce::String getLocalInterface() const;

    /** Prioridad E1.31 (0..200, por defecto 100). */
    void setPriority (int p) noexcept { priority.store ((juce::uint8) juce::jlimit (0, 200, p)); }
    int  getPriority() const noexcept { return priority.load(); }

    /** Reinicia los contadores de secuencia. */
    void reset() noexcept;

    /** Envia un universo DMX (E1.31). universe: 1..63999.
        numChannels: 1..512. */
    void sendUniverse (int universe, const juce::uint8* data, int numChannels);

private:
    bool ensureSocket();
    static juce::String multicastAddressForUniverse (int universe);

    std::atomic<bool>       enabled   { false };
    std::atomic<bool>       multicast { true };
    std::atomic<juce::uint8> priority { 100 };

    mutable juce::CriticalSection ipLock;
    juce::String targetIp;
    juce::String localInterface;

    std::unique_ptr<juce::DatagramSocket> socket;   // protegido por socketLock
    juce::CriticalSection socketLock;

    // CID estable de esta fuente (16 bytes), generado una vez.
    std::array<juce::uint8, 16> cid {};

    // Contador de secuencia por universo (indexado por universe % 512).
    std::array<juce::uint8, 512> sequence {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SacnSender)
};
