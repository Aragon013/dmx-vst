#include "EnttecSender.h"

void EnttecSender::setEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == enabled.load())
        return;

    if (shouldBeEnabled)
    {
        enabled.store (true);
        ensureOpen();
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::high);
    }
    else
    {
        enabled.store (false);
        stopThread (1000);
        const juce::ScopedLock sl (serialLock);
        serial.close();
        connected.store (false);
    }
}

void EnttecSender::setPort (const juce::String& newPort)
{
    {
        const juce::ScopedLock pl (portLock);
        portName = newPort.trim();
    }

    const juce::ScopedLock sl (serialLock);
    serial.close();
    connected.store (false);

    if (enabled.load())
        ensureOpen();
}

juce::String EnttecSender::getPort() const
{
    const juce::ScopedLock pl (portLock);
    return portName;
}

bool EnttecSender::ensureOpen()
{
    juce::String p;
    {
        const juce::ScopedLock pl (portLock);
        p = portName;
    }
    if (p.isEmpty())
        return false;

    const juce::ScopedLock sl (serialLock);
    if (serial.isOpen())
        return true;

    const bool ok = serial.open (p, 250000);
    connected.store (ok);
    return ok;
}

void EnttecSender::sendUniverse (const juce::uint8* data, int numChannels)
{
    if (data == nullptr)
        return;

    const int len = juce::jlimit (1, 512, numChannels);

    // Solo actualizamos el buffer compartido; el hilo dedicado lo emite a
    // cadencia constante. Esto evita el parpadeo por timing irregular.
    for (int c = 0; c < len; ++c)
        channelData[(size_t) c].store (data[c], std::memory_order_relaxed);
    for (int c = len; c < 512; ++c)
        channelData[(size_t) c].store (0, std::memory_order_relaxed);
}

void EnttecSender::writeFrameToSerial (const juce::uint8* data, int len)
{
    const juce::ScopedLock sl (serialLock);
    if (! serial.isOpen())
        return;

    if (protocol.load() == Protocol::OpenDmx)
    {
        // Modo Open DMX (FTDI directo): el host genera el frame DMX completo.
        // 1) BREAK (linea a 0) ~150us, 2) MAB (linea a 1) ~12us,
        // 3) start code (0x00) + canales, crudos a 250 kbaud 8N2.
        std::array<juce::uint8, 513> frame {};
        frame[0] = 0x00;                       // DMX start code
        for (int c = 0; c < len; ++c)
            frame[(size_t) (c + 1)] = data[c];

        serial.setBreak (true);
        const double breakUntil = juce::Time::getMillisecondCounterHiRes() + 0.15;
        while (juce::Time::getMillisecondCounterHiRes() < breakUntil) {}

        serial.setBreak (false);
        const double mabUntil = juce::Time::getMillisecondCounterHiRes() + 0.012;
        while (juce::Time::getMillisecondCounterHiRes() < mabUntil) {}

        if (! serial.write (frame.data(), 1 + len))
        {
            serial.close();
            connected.store (false);
        }
        return;
    }

    // Modo Enttec USB Pro: el dispositivo genera el timing DMX.
    const int payload = len + 1;   // start code (0x00) + canales DMX

    std::array<juce::uint8, 5 + 513> msg {};
    int i = 0;
    msg[(size_t) i++] = 0x7E;                              // inicio de mensaje
    msg[(size_t) i++] = 6;                                 // label: Output Only Send DMX
    msg[(size_t) i++] = (juce::uint8) (payload & 0xFF);    // longitud LSB
    msg[(size_t) i++] = (juce::uint8) ((payload >> 8) & 0xFF); // longitud MSB
    msg[(size_t) i++] = 0x00;                              // DMX start code
    for (int c = 0; c < len; ++c)
        msg[(size_t) i++] = data[c];
    msg[(size_t) i++] = 0xE7;                              // fin de mensaje

    if (! serial.write (msg.data(), i))
    {
        serial.close();
        connected.store (false);
    }
}

void EnttecSender::run()
{
    // Emite el buffer DMX a cadencia constante (~40 Hz) mientras este activo.
    while (! threadShouldExit())
    {
        if (enabled.load() && ensureOpen())
        {
            std::array<juce::uint8, 512> snapshot {};
            for (int c = 0; c < 512; ++c)
                snapshot[(size_t) c] = channelData[(size_t) c].load (std::memory_order_relaxed);

            writeFrameToSerial (snapshot.data(), 512);
        }

        wait (25);   // ~40 fps; el DMX necesita refresco continuo
    }
}

