#pragma once

#include "SerialPort.h"
#include <atomic>
#include <array>

/**
    Salida DMX por hardware Enttec USB Pro / Open DMX (y compatibles FTDI).

    Soporta dos protocolos:
      - UsbPro:  mensaje "Output Only Send DMX" (0x7E .. 0xE7). El propio Enttec
                 genera el timing DMX (break, MAB, 250 kbaud).
      - OpenDmx: cables FTDI genericos USB-XLR. El host genera el BREAK + MAB y
                 envia los bytes DMX crudos a 250 kbaud.

    El envio corre en un HILO DEDICADO a cadencia constante (~40 Hz) para que el
    refresco DMX sea estable y no parpadee (no depende del temporizador de la UI).
    La UI/el motor solo actualizan el buffer de 512 canales con sendUniverse().

    Un solo dispositivo = 1 universo DMX. Aqui emitimos UN universo elegible.
*/
class EnttecSender : private juce::Thread
{
public:
    EnttecSender() : juce::Thread ("DmxOutput") {}
    ~EnttecSender() override { stopThread (1000); }

    /** Protocolo del dispositivo:
        - UsbPro:  Enttec USB Pro y compatibles (mensaje 0x7E..0xE7, el aparato
                   genera el timing DMX). 
        - OpenDmx: Enttec Open DMX USB y cables FTDI genericos USB-XLR. El host
                   genera el BREAK + MAB y envia los bytes DMX crudos a 250 kbaud. */
    enum class Protocol { UsbPro, OpenDmx };

    void setProtocol (Protocol p) noexcept { protocol.store (p); }
    Protocol getProtocol() const noexcept  { return protocol.load(); }

    /** Activa/desactiva. Al activar intenta abrir el puerto guardado. */
    void setEnabled (bool shouldBeEnabled);
    bool isEnabled() const noexcept { return enabled.load(); }

    /** Puerto a usar (p.ej. "COM3"). Si esta abierto, lo reabre. */
    void setPort (const juce::String& portName);
    juce::String getPort() const;

    /** Universo interno (0..n) cuyo contenido se envia por este dispositivo. */
    void setUniverse (int u) noexcept { universe.store (juce::jmax (0, u)); }
    int  getUniverse() const noexcept { return universe.load(); }

    /** true si el puerto esta abierto correctamente. */
    bool isConnected() const noexcept { return connected.load(); }

    /** Actualiza el buffer DMX (1..512 canales) que el hilo emite continuamente. */
    void sendUniverse (const juce::uint8* data, int numChannels);

    /** Puertos serie disponibles (para poblar el combo de la UI). */
    static juce::StringArray getAvailablePorts() { return SerialPort::getAvailablePorts(); }

private:
    void run() override;                 // hilo de envio DMX a cadencia constante
    bool ensureOpen();
    void writeFrameToSerial (const juce::uint8* data, int len);

    std::atomic<bool> enabled   { false };
    std::atomic<bool> connected { false };
    std::atomic<int>  universe  { 0 };
    std::atomic<Protocol> protocol { Protocol::UsbPro };

    // Buffer DMX compartido (512 canales). Lock-free: la UI escribe, el hilo lee.
    std::array<std::atomic<juce::uint8>, 512> channelData {};

    mutable juce::CriticalSection portLock;
    juce::String portName;

    SerialPort serial;            // protegido por serialLock
    juce::CriticalSection serialLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnttecSender)
};

