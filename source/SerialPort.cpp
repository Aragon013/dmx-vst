#include "SerialPort.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <setupapi.h>
 #include <devguid.h>
 #pragma comment(lib, "Setupapi.lib")
#else
 #include <fcntl.h>
 #include <termios.h>
 #include <unistd.h>
 #include <sys/ioctl.h>
#endif

namespace
{
    // Extrae COMx de un texto como "USB Serial Device (COM4)" o "COM4 - ...".
    juce::String extractComPortName (const juce::String& text)
    {
        const auto upper = text.toUpperCase();
        for (int pos = 0; pos + 3 <= upper.length(); ++pos)
        {
            if (upper[pos] != 'C' || upper[pos + 1] != 'O' || upper[pos + 2] != 'M')
                continue;

            int end = pos + 3;
            while (end < upper.length() && upper[end] >= '0' && upper[end] <= '9')
                ++end;

            if (end > pos + 3)
                return "COM" + upper.substring (pos + 3, end);
        }

        return text.trim();
    }
}

SerialPort::~SerialPort()
{
    close();
}

#if JUCE_WINDOWS

bool SerialPort::open (const juce::String& portName, int baudRate)
{
    close();

    const auto normalizedPort = extractComPortName (portName);

    // Para COM10 y superiores hay que usar el prefijo \\.\ .
    juce::String path = "\\\\.\\" + normalizedPort;

    HANDLE h = CreateFileA (path.toRawUTF8(),
                            GENERIC_READ | GENERIC_WRITE,
                            0,            // sin compartir
                            nullptr,
                            OPEN_EXISTING,
                            0,            // E/S bloqueante
                            nullptr);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    DCB dcb {};
    dcb.DCBlength = sizeof (dcb);
    if (! GetCommState (h, &dcb))
    {
        CloseHandle (h);
        return false;
    }

    dcb.BaudRate = (DWORD) baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = TWOSTOPBITS;   // DMX = 8N2
    dcb.Parity   = NOPARITY;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;

    if (! SetCommState (h, &dcb))
    {
        CloseHandle (h);
        return false;
    }

    COMMTIMEOUTS to {};
    to.WriteTotalTimeoutConstant   = 100;
    to.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts (h, &to);

    PurgeComm (h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    handle      = h;
    handleValid = true;
    currentPort = normalizedPort;
    return true;
}

void SerialPort::close()
{
    if (handleValid && handle != nullptr)
        CloseHandle ((HANDLE) handle);
    handle      = nullptr;
    handleValid = false;
    currentPort = {};
}

bool SerialPort::write (const void* data, int numBytes)
{
    if (! handleValid || handle == nullptr || numBytes <= 0)
        return false;

    DWORD written = 0;
    if (! WriteFile ((HANDLE) handle, data, (DWORD) numBytes, &written, nullptr))
        return false;

    return (int) written == numBytes;
}

void SerialPort::setBreak (bool on)
{
    if (! handleValid || handle == nullptr)
        return;

    if (on) SetCommBreak ((HANDLE) handle);
    else    ClearCommBreak ((HANDLE) handle);
}

void SerialPort::drain()
{
    if (handleValid && handle != nullptr)
        FlushFileBuffers ((HANDLE) handle);
}

juce::StringArray SerialPort::getAvailablePorts()
{
    juce::StringArray ports;

    HDEVINFO devInfo = SetupDiGetClassDevsW (&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo != INVALID_HANDLE_VALUE)
    {
        for (DWORD i = 0; ; ++i)
        {
            SP_DEVINFO_DATA devData {};
            devData.cbSize = sizeof (devData);

            if (! SetupDiEnumDeviceInfo (devInfo, i, &devData))
                break;

            WCHAR friendly[512] {};
            DWORD type = 0;
            if (! SetupDiGetDeviceRegistryPropertyW (devInfo, &devData, SPDRP_FRIENDLYNAME,
                                                     &type, reinterpret_cast<PBYTE> (friendly),
                                                     sizeof (friendly), nullptr))
                continue;

            const juce::String friendlyName (friendly);
            const auto com = extractComPortName (friendlyName);
            if (com.startsWithIgnoreCase ("COM"))
                ports.addIfNotAlreadyThere (com + " - " + friendlyName);
        }

        SetupDiDestroyDeviceInfoList (devInfo);
    }

    // Fallback: si no se pudo enumerar friendly names, al menos listamos COMx presentes.
    if (ports.isEmpty())
    {
        for (int i = 1; i <= 256; ++i)
        {
            juce::String name = "COM" + juce::String (i);
            juce::String path = "\\\\.\\" + name;
            HANDLE h = CreateFileA (path.toRawUTF8(), GENERIC_READ | GENERIC_WRITE,
                                    0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                ports.add (name);
                CloseHandle (h);
            }
            else if (GetLastError() == ERROR_ACCESS_DENIED)
            {
                ports.add (name);   // existe pero esta en uso
            }
        }
    }

    return ports;
}

#else  // ---- POSIX (macOS / Linux) ----

bool SerialPort::open (const juce::String& portName, int baudRate)
{
    close();

    int f = ::open (portName.toRawUTF8(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (f < 0)
        return false;

    termios tty {};
    if (tcgetattr (f, &tty) != 0)
    {
        ::close (f);
        return false;
    }

    cfmakeraw (&tty);

    // Baud: muchos sistemas no aceptan 250000 via cfsetspeed estandar; el Enttec
    // USB Pro usa su propio reloj, asi que un baudrate alto estandar sirve.
    speed_t spd = B115200;
   #ifdef B230400
    if (baudRate >= 230400) spd = B230400;
   #endif
    cfsetispeed (&tty, spd);
    cfsetospeed (&tty, spd);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;            // sin paridad
    tty.c_cflag |= CSTOPB;             // 2 stop bits (DMX 8N2)
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                // 8 bits

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr (f, TCSANOW, &tty) != 0)
    {
        ::close (f);
        return false;
    }

    // Volvemos a modo bloqueante para las escrituras.
    fcntl (f, F_SETFL, 0);

    fd          = f;
    handleValid = true;
    currentPort = portName;
    return true;
}

void SerialPort::close()
{
    if (handleValid && fd >= 0)
        ::close (fd);
    fd          = -1;
    handleValid = false;
    currentPort = {};
}

bool SerialPort::write (const void* data, int numBytes)
{
    if (! handleValid || fd < 0 || numBytes <= 0)
        return false;

    const auto* p = static_cast<const juce::uint8*> (data);
    int remaining = numBytes;
    while (remaining > 0)
    {
        const auto n = ::write (fd, p, (size_t) remaining);
        if (n <= 0)
            return false;
        p         += n;
        remaining -= (int) n;
    }
    return true;
}

void SerialPort::setBreak (bool on)
{
    if (! handleValid || fd < 0)
        return;

    ioctl (fd, on ? TIOCSBRK : TIOCCBRK);
}

void SerialPort::drain()
{
    if (handleValid && fd >= 0)
        tcdrain (fd);
}

juce::StringArray SerialPort::getAvailablePorts()
{
    juce::StringArray ports;
    juce::File dev ("/dev");
    for (const auto& f : dev.findChildFiles (juce::File::findFiles, false))
    {
        const auto name = f.getFileName();
        if (name.startsWith ("tty.usb") || name.startsWith ("cu.usb")
            || name.startsWith ("ttyUSB") || name.startsWith ("ttyACM"))
            ports.add (f.getFullPathName());
    }
    return ports;
}

#endif
