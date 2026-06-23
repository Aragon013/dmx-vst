#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "StemSeparator.h"
#include <mutex>

/**
    Backend de separacion de stems basado en HTDemucs (Demucs v4) invocado como
    PROCESO Python externo (camino A).

    Por que un proceso y no ONNX nativo: HTDemucs es un modelo hibrido (rama de
    forma de onda + rama espectral + transformer); no existe un .onnx oficial y
    reimplementar su pipeline en C++ seria enorme y fragil. Invocar `demucs`
    como proceso da el modelo REAL de Meta, gratis (MIT), offline y cacheado.

    Requisitos en la maquina (per-user, sin admin):
      python -m pip install demucs torch==2.2.2 torchaudio==2.2.2 soundfile "numpy<2"

    Para no depender de ffmpeg en tiempo de ejecucion, decodificamos el audio de
    entrada a un WAV temporal con JUCE y se lo pasamos a demucs (soundfile lee
    WAV sin DLLs externas). La salida son 4 WAV: drums/bass/vocals/other.

    El resultado se cachea en `<cacheDir>/<firma>/htdemucs/<stem>.wav`; si ya
    existe, se reutiliza sin volver a separar.
*/
class DemucsProcessProvider : public StemSeparator
{
public:
    DemucsProcessProvider()
    {
        formatManager.registerBasicFormats();
        detectPython();
    }

    bool isAvailable() const override { return pythonOk; }

    juce::String getBackendName() const override
    {
        return pythonOk ? ("HTDemucs (" + modelName + ")") : juce::String ("HTDemucs (no disponible)");
    }

    /** Prefijo de invocacion de Python detectado (para diagnostico). */
    juce::StringArray getPythonPrefix() const { return pythonPrefix; }

    void clearCache (const juce::File& input, const juce::File& cacheDir) override
    {
        if (cacheDir != juce::File())
            cacheDir.getChildFile (signatureFor (input)).deleteRecursively();
    }

    StemSet separate (const juce::File& input,
                      const juce::File& cacheDir,
                      const std::function<bool()>& shouldAbort) override
    {
        StemSet result;

        if (! pythonOk || ! input.existsAsFile())
            return result;

        // Carpeta de cache estable por firma del archivo (tamano + fecha + ruta).
        const juce::File stemDir  = cacheDir.getChildFile (signatureFor (input));
        const juce::File modelDir = stemDir.getChildFile (modelName);

        // 1) Cache hit: si ya estan los 4 stems, reutilizar (sin tomar el lock).
        if (auto cached = collectStems (modelDir); cached.valid)
            return cached;

        if (shouldAbort && shouldAbort())
            return result;

        // demucs es MUY pesado en CPU (satura todos los nucleos). Serializamos la
        // separacion: solo un tema se separa a la vez aunque haya varios en cola.
        std::lock_guard<std::mutex> serialize (separateMutex);

        // Re-comprobar cache: otro hilo pudo separar este mismo tema mientras
        // esperabamos el lock.
        if (auto cached = collectStems (modelDir); cached.valid)
            return cached;

        if (shouldAbort && shouldAbort())
            return result;

        stemDir.createDirectory();

        // 2) Decodificar a WAV temporal (sin depender de ffmpeg en demucs).
        const juce::File wav = decodeToWav (input, stemDir);
        if (wav == juce::File())
            return result;

        if (shouldAbort && shouldAbort())
        {
            wav.deleteFile();
            return result;
        }

        // 3) Invocar demucs: salida plana <stemDir>/<modelName>/<stem>.wav
        juce::StringArray args (pythonPrefix);
        args.add ("-m");
        args.add ("demucs");
        args.add ("-n");           args.add (modelName);
        args.add ("--filename");   args.add ("{stem}.{ext}");
        args.add ("-o");           args.add (stemDir.getFullPathName());
        args.add (wav.getFullPathName());

        const bool ranOk = runProcess (args, shouldAbort);
        wav.deleteFile();   // ya no necesitamos la copia WAV

        if (! ranOk)
            return result;

        return collectStems (modelDir);
    }

private:
    //==============================================================================
    void detectPython()
    {
        // Permite forzar el interprete por variable de entorno.
        const auto envPy = juce::SystemStats::getEnvironmentVariable ("LUX_PYTHON", {});
        std::vector<juce::StringArray> candidates;
        if (envPy.isNotEmpty())
            candidates.push_back ({ envPy });
        candidates.push_back ({ "python" });
        candidates.push_back ({ "py", "-3" });
        candidates.push_back ({ "python3" });

        for (const auto& prefix : candidates)
        {
            juce::StringArray probe (prefix);
            probe.add ("--version");

            juce::ChildProcess proc;
            if (proc.start (probe, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            {
                const juce::String out = proc.readAllProcessOutput();
                proc.waitForProcessToFinish (8000);
                if (out.containsIgnoreCase ("Python"))
                {
                    pythonPrefix = prefix;
                    pythonOk = true;
                    return;
                }
            }
        }
        pythonOk = false;
    }

    /** Lanza el proceso, drena su salida y permite cancelar. */
    bool runProcess (const juce::StringArray& args, const std::function<bool()>& shouldAbort)
    {
        juce::ChildProcess proc;
        if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return false;

        for (;;)
        {
            char buffer[4096];
            const int n = proc.readProcessOutput (buffer, (int) sizeof (buffer));
            if (n > 0)
            {
                // (Salida de demucs ignorada; se podria exponer como progreso.)
                if (shouldAbort && shouldAbort())
                {
                    proc.kill();
                    return false;
                }
                continue;
            }
            if (! proc.isRunning())
                break;

            if (shouldAbort && shouldAbort())
            {
                proc.kill();
                return false;
            }
            juce::Thread::sleep (100);
        }

        proc.waitForProcessToFinish (3000);
        return proc.getExitCode() == 0;
    }

    /** Decodifica cualquier formato soportado a un WAV 16-bit en `workDir`. */
    juce::File decodeToWav (const juce::File& input, const juce::File& workDir)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (input));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return {};

        const juce::File out = workDir.getChildFile ("_input.wav");
        out.deleteFile();

        std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
        if (os == nullptr)
            return {};

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (os.get(), reader->sampleRate,
                                 juce::jmax (1u, reader->numChannels), 16, {}, 0));
        if (writer == nullptr)
            return {};

        os.release();   // el writer toma posesion del stream
        const bool ok = writer->writeFromAudioReader (*reader, 0, reader->lengthInSamples);
        writer.reset(); // cierra y libera el stream

        if (! ok)
        {
            out.deleteFile();
            return {};
        }
        return out;
    }

    /** Comprueba que los 4 WAV de stems existen y devuelve el StemSet. */
    StemSet collectStems (const juce::File& modelDir) const
    {
        StemSet s;
        s.drums  = modelDir.getChildFile ("drums.wav");
        s.bass   = modelDir.getChildFile ("bass.wav");
        s.vocals = modelDir.getChildFile ("vocals.wav");
        s.other  = modelDir.getChildFile ("other.wav");

        const bool all = s.drums.existsAsFile()  && s.drums.getSize()  > 0
                      && s.bass.existsAsFile()   && s.bass.getSize()   > 0
                      && s.vocals.existsAsFile() && s.vocals.getSize() > 0
                      && s.other.existsAsFile()  && s.other.getSize()  > 0;
        s.valid = all;
        return s;
    }

    /** Carpeta de cache derivada de ruta + tamano + fecha de modificacion. */
    juce::String signatureFor (const juce::File& input) const
    {
        const juce::String key = input.getFullPathName()
                               + "|" + juce::String (input.getSize())
                               + "|" + juce::String (input.getLastModificationTime().toMilliseconds());
        const auto hash = (juce::uint64) key.hashCode64();
        return input.getFileNameWithoutExtension().retainCharacters (
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
             + "_" + juce::String::toHexString ((juce::int64) hash);
    }

    juce::AudioFormatManager formatManager;
    juce::StringArray        pythonPrefix;
    bool                     pythonOk = false;
    juce::String             modelName { "htdemucs" };
    std::mutex               separateMutex;   // serializa las invocaciones a demucs
};
