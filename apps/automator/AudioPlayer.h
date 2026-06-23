#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Track.h"

/**
    Reproductor de audio del AI Automator (Standalone): abre el dispositivo de
    salida por defecto y reproduce el archivo del tema seleccionado.

    Encapsula AudioDeviceManager + AudioSourcePlayer + AudioTransportSource para
    que el componente de UI no tenga que gestionar el callback de audio.

    En fases posteriores, el PlaybackEngine lock-free leera la coreografia DMX
    pre-calculada junto a este audio; por ahora solo reproduce el sonido.
*/
class AudioPlayer
{
public:
    AudioPlayer()
    {
        formatManager.registerBasicFormats();

        // Restaura el dispositivo de salida elegido en una sesion anterior (si existe).
        std::unique_ptr<juce::XmlElement> savedState;
        const auto stateFile = deviceStateFile();
        if (stateFile.existsAsFile())
            savedState = juce::XmlDocument::parse (stateFile);

        deviceManager.initialise (0, 2, savedState.get(), true);
        sourcePlayer.setSource (&transport);
        deviceManager.addAudioCallback (&sourcePlayer);
    }

    ~AudioPlayer()
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        transport.setSource (nullptr);
        sourcePlayer.setSource (nullptr);
    }

    /** Guarda el dispositivo/configuracion de audio actual para la proxima sesion. */
    void saveDeviceState() const
    {
        if (auto state = deviceManager.createStateXml())
        {
            const auto file = deviceStateFile();
            file.getParentDirectory().createDirectory();
            state->writeTo (file);
        }
    }

    /** Carga un archivo y deja el transporte preparado en 0. Devuelve true si OK. */
    bool load (const juce::File& file)
    {
        stop();

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
        {
            transport.setSource (nullptr);
            readerSource.reset();
            loadedFile = juce::File();
            return false;
        }

        auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        transport.setSource (newSource.get(), 0, nullptr, newSource->getAudioFormatReader()->sampleRate);
        readerSource = std::move (newSource);
        loadedFile = file;
        return true;
    }

    void play()  { if (readerSource != nullptr) transport.start(); }
    void pause() { transport.stop(); }
    void stop()  { transport.stop(); transport.setPosition (0.0); }

    void togglePlay()
    {
        if (transport.isPlaying()) transport.stop();
        else                       play();
    }

    bool   isPlaying()        const { return transport.isPlaying(); }
    bool   hasFile()          const { return readerSource != nullptr; }
    double getPositionSeconds() const { return transport.getCurrentPosition(); }
    double getLengthSeconds()   const { return transport.getLengthInSeconds(); }
    void   setPositionSeconds (double s) { transport.setPosition (s); }
    const juce::File& getLoadedFile() const { return loadedFile; }

    /** true cuando el tema acaba de terminar (para encadenar el siguiente). */
    bool hasReachedEnd() const
    {
        return readerSource != nullptr
            && transport.getLengthInSeconds() > 0.0
            && transport.hasStreamFinished();
    }

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

private:
    static juce::File deviceStateFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LuxSync")
                   .getChildFile ("audio_device.xml");
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer  sourcePlayer;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::File loadedFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPlayer)
};
