// Scrittura su disco dei file ricevuti (L1). Sicurezza minima ma reale:
// - sanifica il nome (niente componenti di path, niente "..": si scrive SOLO
//   dentro la cartella di destinazione);
// - scrive su un file ".part" e rinomina a fine upload, cosi' un upload
//   interrotto non lascia un file dall'aspetto valido;
// - evita di sovrascrivere file esistenti (aggiunge un suffisso numerico).
// Portabile (POSIX): vale su Haiku e su host di sviluppo.
#pragma once

#include <string>

namespace ls {

class FileSink {
public:
    explicit FileSink(std::string destDir);

    // Crea la cartella di destinazione se manca. false su errore (err popolato).
    bool ensureDir(std::string* err = nullptr) const;

    // Salva 'bytes' come 'fileName' (sanificato) dentro destDir. Scrive su .part
    // e rinomina. In outPath il percorso finale. false su errore (err popolato).
    bool save(const std::string& fileName, const std::string& bytes,
              std::string* outPath = nullptr, std::string* err = nullptr) const;

    // Riduce un nome a un nome-file sicuro: solo il basename, mai vuoto/"."/"..".
    static std::string sanitize(const std::string& fileName);

    const std::string& dir() const { return destDir_; }

private:
    // Percorso libero dentro destDir per 'name' (aggiunge -1, -2... se occupato).
    std::string uniquePath(const std::string& name) const;

    std::string destDir_;
};

} // namespace ls
