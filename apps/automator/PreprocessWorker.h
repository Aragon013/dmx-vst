#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "OfflineAnalyzer.h"
#include "SidecarStore.h"
#include "DemucsProcessProvider.h"
#include <functional>

/**
    Pool de workers para el pre-proceso de temas en background.

    Encola un trabajo por tema: comprueba la cache sidecar (.lux); si es valida
    la usa al instante, si no ejecuta el OfflineAnalyzer y la guarda. El resultado
    se entrega en el HILO DE MENSAJES via onComplete, identificando el tema por su
    id estable (no por indice, que puede cambiar al reordenar).

    Es seguro frente a la destruccion: el destructor cancela y espera los trabajos
    en curso, y la WeakReference evita que un callAsync pendiente toque memoria
    liberada.
*/
class PreprocessWorker
{
public:
    PreprocessWorker()
    {
        formatManager.registerBasicFormats();

        // Carpeta de cache de stems: %APPDATA%/LuxSync/stems
        stemCacheDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("LuxSync").getChildFile ("stems");
        stemCacheDir.createDirectory();
    }

    ~PreprocessWorker()
    {
        pool.removeAllJobs (true, 4000);
    }

    /** Callback en el hilo de mensajes: (id del tema, analisis, exito). */
    std::function<void (int, TrackAnalysis, bool)> onComplete;
    /** Callback en el hilo de mensajes al empezar un tema (para marcar "Analizando"). */
    std::function<void (int)> onStarted;

    /** True si el backend de stems (HTDemucs/Python) esta disponible. */
    bool isStemSeparationAvailable() const { return stemSeparator.isAvailable(); }
    juce::String getStemBackendName() const { return stemSeparator.getBackendName(); }

    /** Encola el analisis de un tema.
        wantStems = separar instrumentos con IA (entrenamiento completo). Solo se
        aplica si el backend esta disponible. force = ignora la cache y reentrena. */
    void enqueue (int trackId, const juce::File& file, bool wantStems, bool force = false)
    {
        pool.addJob (new Job (*this, trackId, file, wantStems, force), true);
    }

    void cancelAll()
    {
        pool.removeAllJobs (true, 4000);
    }

private:
    class Job : public juce::ThreadPoolJob
    {
    public:
        Job (PreprocessWorker& o, int id, juce::File f, bool stems, bool forceFresh)
            : juce::ThreadPoolJob ("preprocess"), owner (o), trackId (id), file (std::move (f)),
              wantStems (stems), force (forceFresh) {}

        JobStatus runJob() override
        {
            juce::WeakReference<PreprocessWorker> safeOwner (&owner);

            // Aviso de inicio (hilo de mensajes).
            juce::MessageManager::callAsync ([safeOwner, id = trackId]
            {
                if (auto* o = safeOwner.get(); o != nullptr && o->onStarted)
                    o->onStarted (id);
            });

            // 1) Intentar cache. Si es un re-entrenamiento forzado, ignorarla y
            //    borrar los stems cacheados para que demucs los vuelva a separar.
            if (force)
            {
                SidecarStore::sidecarFor (file).deleteFile();
                if (owner.stemSeparator.isAvailable())
                    owner.stemSeparator.clearCache (file, owner.stemCacheDir);
            }

            TrackAnalysis result = force ? TrackAnalysis {} : SidecarStore::load (file);

            // Si se piden stems pero la cache no los tiene, hay que re-analizar.
            const bool needStems = (wantStems || force) && owner.stemSeparator.isAvailable();
            const bool cacheUsable = (! force) && result.valid && (! needStems || ! result.stems.empty());

            // 2) Si no hay cache valida (o falta stems), analizar y guardar.
            if (! cacheUsable)
            {
                StemSeparator* sep = needStems ? &owner.stemSeparator : nullptr;
                result = OfflineAnalyzer::analyse (file, owner.formatManager,
                                                   [this] { return shouldExit(); },
                                                   sep, owner.stemCacheDir);
                if (shouldExit())
                    return jobHasFinished;
                if (result.valid)
                    SidecarStore::save (file, result);
            }

            const bool ok = result.valid;
            juce::MessageManager::callAsync ([safeOwner, id = trackId, result, ok]
            {
                if (auto* o = safeOwner.get(); o != nullptr && o->onComplete)
                    o->onComplete (id, result, ok);
            });

            return jobHasFinished;
        }

    private:
        PreprocessWorker& owner;
        int       trackId;
        juce::File file;
        bool      wantStems = false;
        bool      force = false;
    };

    juce::AudioFormatManager formatManager;
    DemucsProcessProvider    stemSeparator;
    juce::File               stemCacheDir;
    juce::ThreadPool pool { juce::jmax (1, juce::SystemStats::getNumCpus() - 1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (PreprocessWorker)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreprocessWorker)
};
