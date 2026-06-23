#include "ArtNetSender.h"

void ArtNetSender::setEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == enabled.load())
        return;

    if (shouldBeEnabled)
    {
        ensureSocket();
        enabled.store (true);
    }
    else
    {
        enabled.store (false);
        const juce::ScopedLock sl (socketLock);
        socket.reset();
    }
}

void ArtNetSender::setTargetIp (const juce::String& ip)
{
    const juce::ScopedLock sl (ipLock);
    targetIp = ip.trim();
}

juce::String ArtNetSender::getTargetIp() const
{
    const juce::ScopedLock sl (ipLock);
    return targetIp;
}

void ArtNetSender::setLocalInterface (const juce::String& ip)
{
    {
        const juce::ScopedLock il (ipLock);
        if (localInterface == ip.trim())
            return;
        localInterface = ip.trim();
    }
    // Recrear el socket para que se enlace a la nueva interfaz.
    const juce::ScopedLock sl (socketLock);
    socket.reset();
}

juce::String ArtNetSender::getLocalInterface() const
{
    const juce::ScopedLock il (ipLock);
    return localInterface;
}

void ArtNetSender::reset() noexcept
{
    sequence.fill (0);
}

bool ArtNetSender::ensureSocket()
{
    const juce::ScopedLock sl (socketLock);
    if (socket != nullptr)
        return true;

    // enableBroadcasting = true permite tambien el unicast normal.
    socket = std::make_unique<juce::DatagramSocket> (true);

    juce::String iface;
    {
        const juce::ScopedLock il (ipLock);
        iface = localInterface;
    }

    // bind a puerto efimero solo para emitir (a la interfaz elegida si la hay).
    const bool ok = iface.isNotEmpty() ? socket->bindToPort (0, iface)
                                       : socket->bindToPort (0);
    if (! ok)
    {
        socket.reset();
        return false;
    }
    return true;
}

void ArtNetSender::sendUniverse (int universe, const juce::uint8* data, int numChannels)
{
    if (! enabled.load() || data == nullptr)
        return;

    if (! ensureSocket())
        return;

    // Longitud par, entre 2 y 512.
    int len = juce::jlimit (2, 512, numChannels);
    if ((len & 1) != 0)
        ++len;
    len = juce::jmin (len, 512);

    const int u       = juce::jlimit (0, 32767, universe);
    const juce::uint8 subUni = (juce::uint8) (u & 0xFF);
    const juce::uint8 net    = (juce::uint8) ((u >> 8) & 0x7F);

    const int seqIdx = u & 0x1FF;                  // 0..511
    juce::uint8 seq = (juce::uint8) (sequence[(size_t) seqIdx] + 1);
    if (seq == 0) seq = 1;                          // 0 = secuencia deshabilitada
    sequence[(size_t) seqIdx] = seq;

    // ---- Paquete ArtDmx ----
    std::array<juce::uint8, 18 + 512> packet {};
    int i = 0;
    const char* id = "Art-Net";
    for (int k = 0; k < 7; ++k) packet[(size_t) i++] = (juce::uint8) id[k];
    packet[(size_t) i++] = 0;                       // null terminador del id (8 bytes)

    packet[(size_t) i++] = 0x00;                    // OpCode lo  (0x5000 OpDmx, little-endian)
    packet[(size_t) i++] = 0x50;                    // OpCode hi
    packet[(size_t) i++] = 0x00;                    // ProtVerHi (14, big-endian)
    packet[(size_t) i++] = 0x0E;                    // ProtVerLo
    packet[(size_t) i++] = seq;                     // Sequence
    packet[(size_t) i++] = 0x00;                    // Physical
    packet[(size_t) i++] = subUni;                  // SubUni (byte bajo del Port-Address)
    packet[(size_t) i++] = net;                     // Net (byte alto)
    packet[(size_t) i++] = (juce::uint8) ((len >> 8) & 0xFF);  // LengthHi (big-endian)
    packet[(size_t) i++] = (juce::uint8) (len & 0xFF);         // LengthLo

    for (int c = 0; c < len; ++c)
        packet[(size_t) i++] = data[c];

    const int totalBytes = 18 + len;

    juce::String host;
    {
        const juce::ScopedLock il (ipLock);
        host = broadcast.load() ? juce::String ("255.255.255.255") : targetIp;
    }
    if (host.isEmpty())
        return;

    const juce::ScopedLock sl (socketLock);
    if (socket != nullptr)
        socket->write (host, port.load(), packet.data(), totalBytes);
}
