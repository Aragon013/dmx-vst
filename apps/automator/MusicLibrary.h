#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <map>

/**
    Biblioteca de musica tipo "Apple Music" para el AI Automator (Fase 3).

    - Catalogo de temas escaneados MANUALMENTE de carpetas/archivos elegidos por el
      usuario (NO indexa nada automatico).
    - Lee metadata basica (titulo / artista / album / duracion) y la portada
      (incrustada en MP3 ID3v2 APIC o un cover.jpg/folder.jpg de la carpeta).
    - Playlists con nombre (crear / borrar / renombrar / anadir-quitar temas).
    - Persistencia propia en %APPDATA%/LuxSync/music_library.xml.
    - El escaneo corre en un hilo de fondo para no congelar la UI con carpetas
      grandes; los resultados se publican en el hilo de mensajes.

    Es un ChangeBroadcaster: la UI se suscribe para refrescarse.
*/
class MusicLibrary : public juce::ChangeBroadcaster,
                     private juce::Thread
{
public:
    struct MediaItem
    {
        juce::File   file;
        juce::String title;
        juce::String artist;
        juce::String album;
        double       lengthSeconds = 0.0;
        juce::int64  dateAdded     = 0;

        juce::String displayTitle() const
        {
            return title.isNotEmpty() ? title : file.getFileNameWithoutExtension();
        }

        juce::String lengthString() const
        {
            if (lengthSeconds <= 0.0) return "--:--";
            const int total = (int) (lengthSeconds + 0.5);
            return juce::String (total / 60).paddedLeft ('0', 2)
                 + ":" + juce::String (total % 60).paddedLeft ('0', 2);
        }
    };

    struct LibPlaylist
    {
        juce::String     name;
        juce::StringArray paths;   // rutas absolutas de los temas
    };

    MusicLibrary() : juce::Thread ("LibraryScan")
    {
        formatManager.registerBasicFormats();
        load();
    }

    ~MusicLibrary() override
    {
        stopThread (2000);
    }

    //==============================================================================
    // Acceso al catalogo / playlists.

    const std::vector<MediaItem>& items() const noexcept { return catalog; }
    int numItems() const noexcept { return (int) catalog.size(); }

    const std::vector<LibPlaylist>& playlists() const noexcept { return userPlaylists; }
    int numPlaylists() const noexcept { return (int) userPlaylists.size(); }

    juce::String getSupportedWildcard() const { return formatManager.getWildcardForAllFormats(); }

    bool isScanning() const noexcept { return scanning.load(); }
    int  pendingScanCount() const noexcept { return pendingCount.load(); }

    //==============================================================================
    // Anadir contenido (escaneo manual). Lanza el hilo de fondo.

    void addFolder (const juce::File& folder)
    {
        if (! folder.isDirectory()) return;
        {
            const juce::ScopedLock sl (queueLock);
            scanQueue.add (folder);
        }
        startScanIfNeeded();
    }

    void addFiles (const juce::Array<juce::File>& files)
    {
        {
            const juce::ScopedLock sl (queueLock);
            for (const auto& f : files)
                if (f.existsAsFile())
                    scanQueue.add (f);
        }
        startScanIfNeeded();
    }

    /** Quita un tema del catalogo (y de todas las playlists). */
    void removeItem (int index)
    {
        if (index < 0 || index >= (int) catalog.size()) return;
        const auto path = catalog[(size_t) index].file.getFullPathName();
        catalog.erase (catalog.begin() + index);
        for (auto& pl : userPlaylists)
            pl.paths.removeString (path);
        save();
        sendChangeMessage();
    }

    void clearCatalog()
    {
        catalog.clear();
        save();
        sendChangeMessage();
    }

    //==============================================================================
    // Playlists.

    int createPlaylist (const juce::String& name)
    {
        LibPlaylist pl;
        pl.name = name.trim().isNotEmpty() ? name.trim() : "Lista";
        userPlaylists.push_back (pl);
        save();
        sendChangeMessage();
        return (int) userPlaylists.size() - 1;
    }

    void removePlaylist (int index)
    {
        if (index < 0 || index >= (int) userPlaylists.size()) return;
        userPlaylists.erase (userPlaylists.begin() + index);
        save();
        sendChangeMessage();
    }

    void renamePlaylist (int index, const juce::String& name)
    {
        if (index < 0 || index >= (int) userPlaylists.size()) return;
        if (name.trim().isEmpty()) return;
        userPlaylists[(size_t) index].name = name.trim();
        save();
        sendChangeMessage();
    }

    void addToPlaylist (int playlistIndex, const juce::File& file)
    {
        if (playlistIndex < 0 || playlistIndex >= (int) userPlaylists.size()) return;
        const auto path = file.getFullPathName();
        auto& pl = userPlaylists[(size_t) playlistIndex];
        if (! pl.paths.contains (path))
            pl.paths.add (path);
        save();
        sendChangeMessage();
    }

    void removeFromPlaylist (int playlistIndex, const juce::File& file)
    {
        if (playlistIndex < 0 || playlistIndex >= (int) userPlaylists.size()) return;
        userPlaylists[(size_t) playlistIndex].paths.removeString (file.getFullPathName());
        save();
        sendChangeMessage();
    }

    /** Indices del catalogo (en orden) que pertenecen a una playlist. -1 = toda la biblioteca. */
    std::vector<int> itemsForPlaylist (int playlistIndex) const
    {
        std::vector<int> result;
        if (playlistIndex < 0)
        {
            for (int i = 0; i < (int) catalog.size(); ++i)
                result.push_back (i);
            return result;
        }
        if (playlistIndex >= (int) userPlaylists.size())
            return result;

        const auto& pl = userPlaylists[(size_t) playlistIndex];
        for (const auto& path : pl.paths)
        {
            const int idx = indexForPath (path);
            if (idx >= 0)
                result.push_back (idx);
        }
        return result;
    }

    /** Filtra una lista de indices por texto (titulo / artista / album). */
    std::vector<int> filterIndices (const std::vector<int>& source, const juce::String& query) const
    {
        const auto q = query.trim().toLowerCase();
        if (q.isEmpty())
            return source;

        std::vector<int> result;
        for (int idx : source)
        {
            if (idx < 0 || idx >= (int) catalog.size()) continue;
            const auto& it = catalog[(size_t) idx];
            const auto hay = (it.displayTitle() + " " + it.artist + " " + it.album).toLowerCase();
            if (hay.contains (q))
                result.push_back (idx);
        }
        return result;
    }

    int indexForPath (const juce::String& path) const
    {
        for (int i = 0; i < (int) catalog.size(); ++i)
            if (catalog[(size_t) i].file.getFullPathName() == path)
                return i;
        return -1;
    }

    //==============================================================================
    // Portada (cacheada por carpeta o por archivo). Devuelve imagen invalida si no hay.

    juce::Image getArtwork (int index)
    {
        if (index < 0 || index >= (int) catalog.size())
            return {};
        return loadArtworkFor (catalog[(size_t) index].file);
    }

    //==============================================================================
    static juce::File defaultLibraryFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LuxSync")
                   .getChildFile ("music_library.xml");
    }

private:
    //==============================================================================
    // Hilo de escaneo.

    void startScanIfNeeded()
    {
        const juce::ScopedLock sl (queueLock);
        pendingCount = scanQueue.size();
        if (! isThreadRunning())
            startThread (Priority::low);
        sendChangeMessage();
    }

    void run() override
    {
        scanning = true;
        for (;;)
        {
            juce::File next;
            {
                const juce::ScopedLock sl (queueLock);
                if (scanQueue.isEmpty()) break;
                next = scanQueue.removeAndReturn (0);
                pendingCount = scanQueue.size();
            }
            if (threadShouldExit()) break;

            if (next.isDirectory())
                scanFolder (next);
            else if (next.existsAsFile())
                ingestFile (next);
        }
        scanning = false;

        juce::MessageManager::callAsync ([this]
        {
            save();
            sendChangeMessage();
        });
    }

    void scanFolder (const juce::File& folder)
    {
        const auto wildcard = formatManager.getWildcardForAllFormats();
        for (juce::DirectoryEntry entry : juce::RangedDirectoryIterator (
                 folder, true, wildcard, juce::File::findFiles))
        {
            if (threadShouldExit()) return;
            ingestFile (entry.getFile());
        }
    }

    void ingestFile (const juce::File& file)
    {
        const auto path = file.getFullPathName();
        {
            const juce::ScopedLock sl (catalogLock);
            for (const auto& it : catalog)
                if (it.file.getFullPathName() == path)
                    return;   // ya esta
        }

        MediaItem item;
        item.file      = file;
        item.dateAdded = juce::Time::getCurrentTime().toMilliseconds();
        readMetadata (item);

        {
            const juce::ScopedLock sl (catalogLock);
            catalog.push_back (item);
        }

        // Refresco incremental en la UI (con un pequeno throttling implicito por lote).
        juce::MessageManager::callAsync ([this] { sendChangeMessage(); });
    }

    //==============================================================================
    // Lectura de metadata.

    void readMetadata (MediaItem& item)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (item.file));
        if (reader != nullptr && reader->sampleRate > 0.0)
        {
            item.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
            readFromMetadataValues (reader->metadataValues, item);
        }

        // MP3: leer ID3v2 directamente (JUCE no expone bien los tags ID3).
        if (item.file.getFileExtension().equalsIgnoreCase (".mp3")
            && (item.title.isEmpty() || item.artist.isEmpty()))
            readId3v2 (item);

        // Fallback: deducir de "Artista - Titulo" del nombre del archivo.
        if (item.title.isEmpty() || item.artist.isEmpty())
            deduceFromFilename (item);
    }

    static void readFromMetadataValues (const juce::StringPairArray& meta, MediaItem& item)
    {
        auto pick = [&meta] (std::initializer_list<const char*> keys) -> juce::String
        {
            for (const auto* k : keys)
            {
                const auto v = meta.getValue (k, {});
                if (v.isNotEmpty()) return v;
            }
            // busqueda insensible a mayusculas
            for (const auto* k : keys)
            {
                const juce::String want (k);
                const auto& all = meta.getAllKeys();
                for (int i = 0; i < all.size(); ++i)
                    if (all[i].equalsIgnoreCase (want))
                        return meta.getAllValues()[i];
            }
            return {};
        };

        if (item.title.isEmpty())  item.title  = pick ({ "title", "TITLE", "tracktitle", "INAM", "TIT2" });
        if (item.artist.isEmpty()) item.artist = pick ({ "artist", "ARTIST", "IART", "TPE1", "author" });
        if (item.album.isEmpty())  item.album  = pick ({ "album", "ALBUM", "IPRD", "TALB" });
    }

    void deduceFromFilename (MediaItem& item)
    {
        const auto base = item.file.getFileNameWithoutExtension();
        const int sep = base.indexOf (" - ");
        if (sep > 0)
        {
            if (item.artist.isEmpty()) item.artist = base.substring (0, sep).trim();
            if (item.title.isEmpty())  item.title  = base.substring (sep + 3).trim();
        }
        else if (item.title.isEmpty())
        {
            item.title = base;
        }
    }

    //==============================================================================
    // Parser minimo de ID3v2 (v2.3 / v2.4): TIT2 / TPE1 / TALB + portada APIC.

    void readId3v2 (MediaItem& item, juce::MemoryBlock* artworkOut = nullptr)
    {
        juce::FileInputStream in (item.file);
        if (! in.openedOk()) return;

        char header[10];
        if (in.read (header, 10) != 10) return;
        if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') return;

        const int major = (juce::uint8) header[3];
        const bool unsync = (header[5] & 0x80) != 0;
        const int tagSize = syncSafe ((const juce::uint8*) (header + 6));
        if (tagSize <= 0 || unsync) return;   // no soportamos unsync global (raro)

        juce::MemoryBlock body;
        body.setSize ((size_t) tagSize);
        if (in.read (body.getData(), tagSize) != tagSize) return;

        const auto* data = (const juce::uint8*) body.getData();
        int pos = 0;
        while (pos + 10 <= tagSize)
        {
            char id[5] = { (char) data[pos], (char) data[pos+1], (char) data[pos+2], (char) data[pos+3], 0 };
            if (id[0] == 0) break;   // padding

            int frameSize;
            if (major >= 4) frameSize = syncSafe (data + pos + 4);
            else            frameSize = (data[pos+4] << 24) | (data[pos+5] << 16) | (data[pos+6] << 8) | data[pos+7];

            pos += 10;
            if (frameSize <= 0 || pos + frameSize > tagSize) break;

            const juce::String fid (id);
            if (fid == "TIT2" || fid == "TPE1" || fid == "TALB")
            {
                const auto text = decodeTextFrame (data + pos, frameSize);
                if (fid == "TIT2" && item.title.isEmpty())  item.title  = text;
                if (fid == "TPE1" && item.artist.isEmpty()) item.artist = text;
                if (fid == "TALB" && item.album.isEmpty())  item.album  = text;
            }
            else if (fid == "APIC" && artworkOut != nullptr && artworkOut->getSize() == 0)
            {
                extractApic (data + pos, frameSize, *artworkOut);
            }

            pos += frameSize;
        }
    }

    static int syncSafe (const juce::uint8* p)
    {
        return ((p[0] & 0x7f) << 21) | ((p[1] & 0x7f) << 14) | ((p[2] & 0x7f) << 7) | (p[3] & 0x7f);
    }

    static juce::String decodeTextFrame (const juce::uint8* p, int size)
    {
        if (size < 1) return {};
        const int enc = p[0];
        const juce::uint8* txt = p + 1;
        const int len = size - 1;

        if (enc == 1 || enc == 2)   // UTF-16 (con/sin BOM) o UTF-16BE
            return juce::String (juce::CharPointer_UTF16 ((const juce::CharPointer_UTF16::CharType*) txt),
                                 (size_t) (len / 2)).trim();
        if (enc == 3)               // UTF-8
            return juce::String::fromUTF8 ((const char*) txt, len).trim();

        // ISO-8859-1 (Latin-1)
        juce::String s;
        for (int i = 0; i < len && txt[i] != 0; ++i)
            s += (juce::juce_wchar) txt[i];
        return s.trim();
    }

    static void extractApic (const juce::uint8* p, int size, juce::MemoryBlock& out)
    {
        // APIC: enc(1) | mime(z) | type(1) | desc(z|zz) | imagen
        int i = 0;
        const int enc = p[i++];
        // mime (ISO-8859-1 terminado en 0)
        while (i < size && p[i] != 0) ++i;
        if (i >= size) return;
        ++i;                      // salta el 0 del mime
        if (i >= size) return;
        ++i;                      // picture type (1 byte)
        // descripcion: 0 (latin1) o 00 00 (utf-16)
        if (enc == 1 || enc == 2)
        {
            while (i + 1 < size && ! (p[i] == 0 && p[i+1] == 0)) i += 2;
            i += 2;
        }
        else
        {
            while (i < size && p[i] != 0) ++i;
            ++i;
        }
        if (i >= size) return;
        out.append (p + i, (size_t) (size - i));
    }

    //==============================================================================
    // Portada con cache (por carpeta para covers, por archivo para incrustadas).

    juce::Image loadArtworkFor (const juce::File& file)
    {
        const auto folderKey = file.getParentDirectory().getFullPathName();

        {
            const juce::ScopedLock sl (artLock);
            auto it = folderArt.find (folderKey);
            if (it != folderArt.end())
                return it->second;
        }

        juce::Image img = findFolderCover (file.getParentDirectory());

        // Si no hay cover en la carpeta, intenta la portada incrustada del MP3.
        if (img.isNull() && file.getFileExtension().equalsIgnoreCase (".mp3"))
        {
            MediaItem tmp; tmp.file = file;
            juce::MemoryBlock art;
            readId3v2 (tmp, &art);
            if (art.getSize() > 0)
                img = juce::ImageFileFormat::loadFrom (art.getData(), art.getSize());
        }

        {
            const juce::ScopedLock sl (artLock);
            folderArt[folderKey] = img;   // cachea incluso si es nula (evita reintentar)
        }
        return img;
    }

    static juce::Image findFolderCover (const juce::File& folder)
    {
        const char* names[] = { "cover", "folder", "front", "album", "albumart", "albumartsmall" };
        const char* exts[]  = { ".jpg", ".jpeg", ".png" };
        for (const auto* n : names)
            for (const auto* e : exts)
            {
                const auto f = folder.getChildFile (juce::String (n) + e);
                if (f.existsAsFile())
                {
                    auto img = juce::ImageFileFormat::loadFrom (f);
                    if (img.isValid()) return img;
                }
            }
        return {};
    }

    //==============================================================================
    // Persistencia.

    void save() const
    {
        juce::ValueTree state ("LuxMusicLibrary");

        juce::ValueTree cat ("Catalog");
        {
            const juce::ScopedLock sl (catalogLock);
            for (const auto& it : catalog)
            {
                juce::ValueTree n ("Item");
                n.setProperty ("path",   it.file.getFullPathName(), nullptr);
                n.setProperty ("title",  it.title, nullptr);
                n.setProperty ("artist", it.artist, nullptr);
                n.setProperty ("album",  it.album, nullptr);
                n.setProperty ("len",    it.lengthSeconds, nullptr);
                n.setProperty ("added",  it.dateAdded, nullptr);
                cat.appendChild (n, nullptr);
            }
        }
        state.appendChild (cat, nullptr);

        juce::ValueTree pls ("Playlists");
        for (const auto& pl : userPlaylists)
        {
            juce::ValueTree n ("Playlist");
            n.setProperty ("name", pl.name, nullptr);
            n.setProperty ("paths", pl.paths.joinIntoString ("\n"), nullptr);
            pls.appendChild (n, nullptr);
        }
        state.appendChild (pls, nullptr);

        const auto file = defaultLibraryFile();
        file.getParentDirectory().createDirectory();
        if (auto xml = state.createXml())
            xml->writeTo (file, {});
    }

    void load()
    {
        const auto file = defaultLibraryFile();
        if (! file.existsAsFile()) return;

        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr) return;
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;

        catalog.clear();
        auto cat = state.getChildWithName ("Catalog");
        for (int i = 0; i < cat.getNumChildren(); ++i)
        {
            auto n = cat.getChild (i);
            MediaItem it;
            it.file          = juce::File (n.getProperty ("path").toString());
            it.title         = n.getProperty ("title").toString();
            it.artist        = n.getProperty ("artist").toString();
            it.album         = n.getProperty ("album").toString();
            it.lengthSeconds = (double) n.getProperty ("len", 0.0);
            it.dateAdded     = (juce::int64) n.getProperty ("added", 0);
            catalog.push_back (it);
        }

        userPlaylists.clear();
        auto pls = state.getChildWithName ("Playlists");
        for (int i = 0; i < pls.getNumChildren(); ++i)
        {
            auto n = pls.getChild (i);
            LibPlaylist pl;
            pl.name = n.getProperty ("name").toString();
            pl.paths.addTokens (n.getProperty ("paths").toString(), "\n", "");
            pl.paths.removeEmptyStrings();
            userPlaylists.push_back (pl);
        }
    }

    //==============================================================================
    juce::AudioFormatManager formatManager;

    std::vector<MediaItem>   catalog;
    std::vector<LibPlaylist> userPlaylists;

    juce::Array<juce::File> scanQueue;
    juce::CriticalSection   queueLock;
    juce::CriticalSection   catalogLock;
    std::atomic<bool>       scanning { false };
    std::atomic<int>        pendingCount { 0 };

    std::map<juce::String, juce::Image> folderArt;
    juce::CriticalSection               artLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MusicLibrary)
};
