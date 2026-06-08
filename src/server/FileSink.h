// Scrittura su disco dei file ricevuti (L1). Sicurezza minima ma reale:
// - sanifica il nome (niente componenti di path, niente "..": si scrive SOLO
//   dentro la cartella di destinazione);
// - scrive su un file ".part" e rinomina a fine upload, cosi' un upload
//   interrotto non lascia un file dall'aspetto valido;
// - evita di sovrascrivere file esistenti (aggiunge un suffisso numerico).
// Portabile (POSIX): vale su Haiku e su host di sviluppo.
#ifndef _LOCALSEND_FILE_SINK_H
#define _LOCALSEND_FILE_SINK_H

#include <string>

namespace LocalSend {

class FileSink {
public:
	explicit FileSink(std::string destDir);

	// Crea la cartella di destinazione se manca. false su errore.
	bool EnsureDir(std::string* err = nullptr) const;

	// Salva 'bytes' come 'fileName' (sanificato) in destDir. Scrive su .part
	// e rinomina. Imposta il tipo MIME come attributo BFS se non vuoto.
	// In outPath il percorso finale. false su errore.
	bool Save(const std::string& fileName, const std::string& bytes,
		const std::string& mimeType = "",
		std::string* outPath = nullptr, std::string* err = nullptr) const;

	// Nome-file sicuro: solo il basename, mai vuoto/"."/"..".
	static std::string Sanitize(const std::string& fileName);

	const std::string& Dir() const { return fDestDir; }

	// Cambia la cartella di destinazione (per le impostazioni).
	void SetDir(const std::string& dir);

private:
	// Percorso libero in destDir per 'name' (aggiunge -1, -2... se occupato).
	std::string UniquePath(const std::string& name) const;

	std::string fDestDir;
};

} // namespace LocalSend

#endif // _LOCALSEND_FILE_SINK_H
