#include "protocol/Models.h"

namespace ls {

JsonValue DeviceInfo::toJson() const {
    JsonValue v = JsonValue::object();
    v["alias"]       = alias;
    v["version"]     = version;
    v["deviceModel"] = deviceModel;
    v["deviceType"]  = deviceType;
    v["fingerprint"] = fingerprint;
    v["port"]        = port;
    v["protocol"]    = protocol;
    v["download"]    = download;
    return v;
}

JsonValue FileMetadata::toJson() const {
    JsonValue v = JsonValue::object();
    v["id"]       = id;
    v["fileName"] = fileName;
    v["size"]     = size;
    v["fileType"] = fileType;
    v["sha256"]   = sha256.empty()  ? JsonValue(nullptr) : JsonValue(sha256);
    v["preview"]  = preview.empty() ? JsonValue(nullptr) : JsonValue(preview);

    if (!modified.empty() || !accessed.empty()) {
        JsonValue meta = JsonValue::object();
        if (!modified.empty()) meta["modified"] = modified;
        if (!accessed.empty()) meta["accessed"] = accessed;
        v["metadata"] = meta;
    }
    return v;
}

JsonValue buildPrepareUpload(const DeviceInfo& info,
                             const std::vector<FileMetadata>& files) {
    JsonValue root = JsonValue::object();
    root["info"] = info.toJson();

    JsonValue filesObj = JsonValue::object();
    for (const auto& f : files) filesObj[f.id] = f.toJson();
    root["files"] = filesObj;
    return root;
}

PrepareUploadResult parsePrepareUploadResponse(const std::string& body) {
    PrepareUploadResult r;
    JsonValue v = JsonValue::parse(body);
    if (v.has("sessionId")) r.sessionId = v.at("sessionId").asString();
    if (v.has("files")) {
        const JsonValue& files = v.at("files");
        for (const auto& kv : files.items())
            r.fileTokens[kv.first] = kv.second.asString();
    }
    return r;
}

} // namespace ls
