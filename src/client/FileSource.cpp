#include "client/FileSource.h"

#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <cstdio>

namespace ls {

namespace {

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string extLower(const std::string& fileName) {
    size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = fileName.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

std::string iso8601(time_t t) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

} // namespace

std::string guessMimeType(const std::string& fileName) {
    std::string ext = extLower(fileName);
    struct { const char* ext; const char* mime; } table[] = {
        {"png", "image/png"},   {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},   {"webp", "image/webp"},{"bmp", "image/bmp"},
        {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"},
        {"txt", "text/plain"},  {"md", "text/markdown"},{"csv", "text/csv"},
        {"html","text/html"},   {"htm", "text/html"},
        {"json","application/json"}, {"xml", "application/xml"},
        {"zip", "application/zip"},  {"gz", "application/gzip"},
        {"tar", "application/x-tar"},{"7z", "application/x-7z-compressed"},
        {"mp3", "audio/mpeg"},  {"wav", "audio/wav"},  {"flac","audio/flac"},
        {"ogg", "audio/ogg"},   {"m4a", "audio/mp4"},
        {"mp4", "video/mp4"},   {"mkv", "video/x-matroska"}, {"webm","video/webm"},
        {"mov", "video/quicktime"}, {"avi", "video/x-msvideo"},
    };
    for (const auto& e : table)
        if (ext == e.ext) return e.mime;
    return "application/octet-stream";
}

bool buildFileMetadata(const std::string& localPath, const std::string& id,
                       FileMetadata& out) {
    struct stat st;
    if (stat(localPath.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;

    out.id        = id;
    out.fileName  = baseName(localPath);
    out.size      = static_cast<long long>(st.st_size);
    out.fileType  = guessMimeType(out.fileName);
    out.modified  = iso8601(st.st_mtime);
    out.accessed  = iso8601(st.st_atime);
    out.localPath = localPath;
    return true;
}

} // namespace ls
