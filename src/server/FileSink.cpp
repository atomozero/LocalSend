#include "server/FileSink.h"

#include <sys/stat.h>
#include <unistd.h>

#include <File.h>
#include <FindDirectory.h>
#include <NodeInfo.h>
#include <Path.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace LocalSend {

namespace {

// Rende assoluta la cartella di destinazione. Un percorso relativo viene
// risolto contro la HOME dell'utente, NON contro la cartella di lavoro: al
// boot o lanciando dalla Deskbar la CWD e' imprevedibile (/, /boot/home, ...)
// e i file finivano dove nessuno li cercava. Cosi' "./ricevuti" diventa
// sempre "<home>/ricevuti".
std::string
NormalizeDir(std::string dir)
{
	while (dir.size() > 1 && dir.back() == '/')
		dir.pop_back();
	if (dir.empty())
		dir = "Downloads";
	if (dir[0] == '/')
		return dir; // gia' assoluto
	if (dir.rfind("./", 0) == 0)
		dir = dir.substr(2);
	BPath home;
	if (find_directory(B_USER_DIRECTORY, &home) == B_OK && home.Path() != NULL)
		return std::string(home.Path()) + "/" + dir;
	return dir; // fallback: lascia com'e'
}

} // namespace

FileSink::FileSink(std::string destDir)
	:
	fDestDir(NormalizeDir(std::move(destDir)))
{
}


std::string
FileSink::Sanitize(const std::string& fileName)
{
	// Tieni solo cio' che segue l'ultimo separatore: scarta ogni componente di
	// path. Gestisci sia '/' sia '\\' per nomi provenienti da altri OS.
	size_t slash = fileName.find_last_of("/\\");
	std::string base = (slash == std::string::npos)
		? fileName : fileName.substr(slash + 1);
	// Scarta i caratteri di controllo e i NUL.
	std::string clean;
	clean.reserve(base.size());
	for (unsigned char c : base) {
		if (c >= 0x20 && c != 0x7f)
			clean += static_cast<char>(c);
	}
	if (clean.empty() || clean == "." || clean == "..")
		clean = "file";
	return clean;
}


bool
FileSink::EnsureDir(std::string* err) const
{
	struct stat st;
	if (stat(fDestDir.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return true;
		if (err)
			*err = fDestDir + " esiste ma non e' una cartella";
		return false;
	}
	if (mkdir(fDestDir.c_str(), 0755) == 0)
		return true;
	if (err)
		*err = std::string("mkdir ") + fDestDir + ": " + std::strerror(errno);
	return false;
}


std::string
FileSink::UniquePath(const std::string& name) const
{
	std::string base = fDestDir + "/" + name;
	if (access(base.c_str(), F_OK) != 0)
		return base;

	// Inserisci un suffisso prima dell'estensione: "foto.png" -> "foto-1.png".
	size_t dot = name.find_last_of('.');
	std::string stem = (dot == std::string::npos) ? name : name.substr(0, dot);
	std::string ext = (dot == std::string::npos) ? "" : name.substr(dot);

	for (int n = 1; n < 10000; ++n) {
		std::string cand
			= fDestDir + "/" + stem + "-" + std::to_string(n) + ext;
		if (access(cand.c_str(), F_OK) != 0)
			return cand;
	}
	return base; // resa: caso estremo, sovrascrivi
}


bool
FileSink::Save(const std::string& fileName, const std::string& bytes,
	const std::string& mimeType, std::string* outPath,
	std::string* err) const
{
	if (!EnsureDir(err))
		return false;

	std::string finalPath = UniquePath(Sanitize(fileName));
	std::string partPath = finalPath + ".part";

	FILE* f = fopen(partPath.c_str(), "wb");
	if (!f) {
		if (err)
			*err = std::string("apertura ") + partPath + ": "
				+ std::strerror(errno);
		return false;
	}
	bool ok = bytes.empty()
		|| fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	if (fclose(f) != 0)
		ok = false;

	if (!ok) {
		remove(partPath.c_str());
		if (err)
			*err = std::string("scrittura ") + partPath + " fallita";
		return false;
	}
	if (rename(partPath.c_str(), finalPath.c_str()) != 0) {
		remove(partPath.c_str());
		if (err)
			*err = std::string("rename ") + partPath + ": "
				+ std::strerror(errno);
		return false;
	}

	// Imposta il tipo MIME come attributo BFS (Tracker mostra l'icona
	// corretta e apre il file con l'app giusta).
	if (!mimeType.empty()) {
		BFile file(finalPath.c_str(), B_READ_WRITE);
		if (file.InitCheck() == B_OK) {
			BNodeInfo nodeInfo(&file);
			nodeInfo.SetType(mimeType.c_str());
		}
	}

	if (outPath)
		*outPath = finalPath;
	return true;
}

void
FileSink::SetDir(const std::string& dir)
{
	fDestDir = NormalizeDir(dir);
}

} // namespace LocalSend
