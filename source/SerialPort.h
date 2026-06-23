#pragma once

#include <juce_core/juce_core.h>

/**
    Puerto serie minimo y multiplataforma (Windows Win32 / POSIX termios).

    JUCE no incluye una clase de puerto serie, asi que envolvemos la API del SO.
    Pensado para abrir un adaptador FTDI (como el Enttec USB Pro), que aparece
    como un puerto COMx en Windows o /dev/tty.* en macOS/Linux.

    Solo expone lo necesario: abrir/cerrar, escribir un bloque de bytes y listar
    los puertos disponibles. La velocidad real del DMX la marca el dispositivo
    Enttec (que reabre su propio timing); aqui solo enviamos a 8N2.
*/
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    /** Abre el puerto (p.ej. "COM3" en Windows, "/dev/tty.usbserial-XXXX" en mac).
        baud por defecto 250000 (DMX); el Enttec USB Pro ignora el baudrate del
        host y usa el suyo, pero lo configuramos igualmente. */
    bool open (const juce::String& portName, int baudRate = 250000);
    void close();
    bool isOpen() const noexcept { return handleValid; }

    /** Escribe 'numBytes' bytes. Devuelve true si se escribieron todos. */
    bool write (const void* data, int numBytes);

    /** Pone (on=true) o quita (on=false) la condicion de BREAK en la linea TX.
        Necesario para el modo "Open DMX" (FTDI directo), donde el host genera el
        break + MAB del DMX en vez del microcontrolador del Enttec USB Pro. */
    void setBreak (bool on);

    /** Espera a que se vacie el buffer de transmision (TX). */
    void drain();

    juce::String getPortName() const { return currentPort; }

    /** Lista los puertos serie disponibles en el sistema. */
    static juce::StringArray getAvailablePorts();

private:
    juce::String currentPort;
    bool handleValid = false;

  #if JUCE_WINDOWS
    void* handle = nullptr;   // HANDLE
  #else
    int fd = -1;
  #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerialPort)
};
