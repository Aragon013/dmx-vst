#include "SacnSender.h"
#include <cstring>

SacnSender::SacnSender()
{
    // CID aleatorio estable durante la vida de la instancia.
    auto uuid = juce::Uuid();
    const auto* raw = uuid.getRawData();
    for (int i = 0; i < 16; ++i)
        cid[(size_t) i] = raw[i];
}

void SacnSender::setEnabled (bool shouldBeEnabled)
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

void SacnSender::setTargetIp (const juce::String& ip)
{
    const juce::ScopedLock sl (ipLock);
    targetIp = ip.trim();
}

juce::String SacnSender::getTargetIp() const
{
    const juce::ScopedLock sl (ipLock);
    return targetIp;
}

void SacnSender::setLocalInterface (const juce::String& ip)
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

juce::String SacnSender::getLocalInterface() const
{
    const juce::ScopedLock il (ipLock);
    return localInterface;
}

void SacnSender::reset() noexcept
{
    sequence.fill (0);
}

juce::String SacnSender::multicastAddressForUniverse (int universe)
{
    const int u = juce::jlimit (1, 63999, universe);
    const int hi = (u >> 8) & 0xFF;
    const int lo = u & 0xFF;
    return "239.255." + juce::String (hi) + "." + juce::String (lo);
}

bool SacnSender::ensureSocket()
{
    const juce::ScopedLock sl (socketLock);
    if (socket != nullptr)
        return true;

    socket = std::make_unique<juce::DatagramSocket> (true);

    juce::String iface;
    {
        const juce::ScopedLock il (ipLock);
        iface = localInterface;
    }

    const bool ok = iface.isNotEmpty() ? socket->bindToPort (0, iface)
                                       : socket->bindToPort (0);
    if (! ok)
    {
        socket.reset();
        return false;
    }
    return true;
}

void SacnSender::sendUniverse (int universe, const juce::uint8* data, int numChannels)
{
    if (! enabled.load() || data == nullptr)
        return;

    if (! ensureSocket())
        return;

    const int len = juce::jlimit (1, 512, numChannels);
    const int u   = juce::jlimit (1, 63999, universe);

    const int seqIdx = u & 0x1FF;
    const juce::uint8 seq = sequence[(size_t) seqIdx]++;

    // El campo de datos DMP lleva el start code (0x00) + los canales.
    const int propValueCount = len + 1;             // 1 (start code) + len
    const int dmpLen   = 10 + propValueCount;        // cabecera DMP (10) + datos
    const int frameLen = 77 + dmpLen;                // cabecera framing (77) + DMP
    const int rootPdu  = 22 + frameLen;              // tras el campo flags/length raiz

    std::array<juce::uint8, 638> p {};               // 126 cabeceras + 512 datos max
    int i = 0;

    auto put16 = [&p, &i] (int v) { p[(size_t) i++] = (juce::uint8) ((v >> 8) & 0xFF);
                                    p[(size_t) i++] = (juce::uint8) (v & 0xFF); };

    // ---- Root Layer ----
    put16 (0x0010);                                  // Preamble Size
    put16 (0x0000);                                  // Post-amble Size
    const char* acn = "ASC-E1.17";                   // ACN packet identifier (12 bytes)
    for (int k = 0; k < 12; ++k)
        p[(size_t) i++] = (k < 9) ? (juce::uint8) acn[k] : (juce::uint8) 0;
    put16 (0x7000 | (rootPdu & 0x0FFF));             // Flags (0x7) + Length
    p[(size_t) i++] = 0x00; p[(size_t) i++] = 0x00;  // Vector = 0x00000004
    p[(size_t) i++] = 0x00; p[(size_t) i++] = 0x04;
    for (int k = 0; k < 16; ++k)                      // CID
        p[(size_t) i++] = cid[(size_t) k];

    // ---- Framing Layer ----
    put16 (0x7000 | (frameLen & 0x0FFF));            // Flags + Length
    p[(size_t) i++] = 0x00; p[(size_t) i++] = 0x00;  // Vector = 0x00000002
    p[(size_t) i++] = 0x00; p[(size_t) i++] = 0x02;
    const char* srcName = "LuxSync DMX";             // Source Name (64 bytes)
    const int nameLen = (int) std::strlen (srcName);
    for (int k = 0; k < 64; ++k)
        p[(size_t) i++] = (k < nameLen) ? (juce::uint8) srcName[k] : (juce::uint8) 0;
    p[(size_t) i++] = priority.load();               // Priority
    put16 (0x0000);                                  // Sync Address = 0 (sin sync)
    p[(size_t) i++] = seq;                           // Sequence Number
    p[(size_t) i++] = 0x00;                          // Options
    put16 (u);                                       // Universe (1..63999)

    // ---- DMP Layer ----
    put16 (0x7000 | (dmpLen & 0x0FFF));              // Flags + Length
    p[(size_t) i++] = 0x02;                          // Vector = VECTOR_DMP_SET_PROPERTY
    p[(size_t) i++] = 0xA1;                          // Address Type & Data Type
    put16 (0x0000);                                  // First Property Address
    put16 (0x0001);                                  // Address Increment
    put16 (propValueCount);                          // Property value count (1 + len)
    p[(size_t) i++] = 0x00;                          // DMX Start Code
    for (int c = 0; c < len; ++c)
        p[(size_t) i++] = data[c];

    const int totalBytes = i;

    juce::String host;
    {
        const juce::ScopedLock il (ipLock);
        host = multicast.load() ? multicastAddressForUniverse (u) : targetIp;
    }
    if (host.isEmpty())
        return;

    const juce::ScopedLock sl (socketLock);
    if (socket != nullptr)
        socket->write (host, 5568, p.data(), totalBytes);
}
