#include "server/FileSink.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace ls {

FileSink::FileSink(std::string destDir) : destDir_(std::move(destDir)) {
    // Niente slash finale, cosi' la join "dir/name" e' sempre pulita.
    while (destDir_.size() > 1 && destDir_.back() == '/') destDir_.pop_back();
    if (destDir_.empty()) destDir_ = ".";
}

std::string FileSink::sanitize(const std::string& fileName) {
    // Tieni solo cio' che segue l'ultimo separatore: scarta ogni componente di
    // path. Gestisci sia '/' sia '\\' per nomi provenienti da altri OS.
    size_t slash = fileName.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? fileName
                                                     : fileName.substr(slash + 1);
    // Scarta i caratteri di controllo e i NUL.
    std::string clean;
    clean.reserve(base.size());
    for (unsigned char c : base) {
        if (c >= 0x20 && c != 0x7f) clean += static_cast<char>(c);
    }
    if (clean.empty() || clean == "." || clean == "..") clean = "file";
    return clean;
}

bool FileSink::ensureDir(std::string* err) const {
    struct stat st;
    if (stat(destDir_.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return true;
        if (err) *err = destDir_ + " esiste ma non e' una cartella";
        return false;
    }
    if (mkdir(destDir_.c_str(), 0755) == 0) return true;
    if (err) *err = std::string("mkdir ") + destDir_ + ": " + std::strerror(errno);
    return false;
}

std::string FileSink::uniquePath(const std::string& name) const {
    std::string base = destDir_ + "/" + name;
    if (access(base.c_str(), F_OK) != 0) return base;

    // Inserisci un suffisso prima dell'estensione: "foto.png" -> "foto-1.png".
    size_t dot = name.find_last_of('.');
    std::string stem = (dot == std::string::npos) ? name : name.substr(0, dot);
    std::string ext  = (dot == std::string::npos) ? ""   : name.substr(dot);

    for (int n = 1; n < 10000; ++n) {
        std::string cand = destDir_ + "/" + stem + "-" + std::to_string(n) + ext;
        if (access(cand.c_str(), F_OK) != 0) return cand;
    }
    return base; // resa: caso estremo, sovrascrivi
}

bool FileSink::save(const std::string& fileName, const std::string& bytes,
                    std::string* outPath, std::string* err) const {
    if (!ensureDir(err)) return false;

    std::string finalPath = uniquePath(sanitize(fileName));
    std::string partPath  = finalPath + ".part";

    FILE* f = fopen(partPath.c_str(), "wb");
    if (!f) {
        if (err) *err = std::string("apertura ") + partPath + ": " + std::strerror(errno);
        return false;
    }
    bool ok = bytes.empty() ||
              fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (fclose(f) != 0) ok = false;

    if (!ok) {
        remove(partPath.c_str());
        if (err) *err = std::string("scrittura ") + partPath + " fallita";
        return false;
    }
    if (rename(partPath.c_str(), finalPath.c_str()) != 0) {
        remove(partPath.c_str());
        if (err) *err = std::string("rename ") + partPath + ": " + std::strerror(errno);
        return false;
    }
    if (outPath) *outPath = finalPath;
    return true;
}

} // namespace ls
