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

DeviceInfo DeviceInfo::fromJson(const JsonValue& v) {
    DeviceInfo d;
    if (v.has("alias"))       d.alias       = v.at("alias").asString(d.alias);
    if (v.has("version"))     d.version     = v.at("version").asString(d.version);
    if (v.has("deviceModel")) d.deviceModel = v.at("deviceModel").asString(d.deviceModel);
    if (v.has("deviceType"))  d.deviceType  = v.at("deviceType").asString(d.deviceType);
    if (v.has("fingerprint")) d.fingerprint = v.at("fingerprint").asString();
    if (v.has("port"))        d.port        = static_cast<int>(v.at("port").asInt(d.port));
    if (v.has("protocol"))    d.protocol    = v.at("protocol").asString(d.protocol);
    if (v.has("download"))    d.download    = v.at("download").asBool(d.download);
    return d;
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

FileMetadata FileMetadata::fromJson(const JsonValue& v) {
    FileMetadata f;
    if (v.has("id"))       f.id       = v.at("id").asString();
    if (v.has("fileName")) f.fileName = v.at("fileName").asString();
    if (v.has("size"))     f.size     = v.at("size").asInt();
    if (v.has("fileType")) f.fileType = v.at("fileType").asString(f.fileType);
    if (v.has("sha256") && v.at("sha256").isString())  f.sha256  = v.at("sha256").asString();
    if (v.has("preview") && v.at("preview").isString()) f.preview = v.at("preview").asString();
    if (v.has("metadata")) {
        const JsonValue& m = v.at("metadata");
        if (m.has("modified")) f.modified = m.at("modified").asString();
        if (m.has("accessed")) f.accessed = m.at("accessed").asString();
    }
    return f;
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

IncomingPrepareUpload parsePrepareUploadRequest(const std::string& body) {
    IncomingPrepareUpload req;
    JsonValue v = JsonValue::parse(body);
    if (v.has("info")) req.sender = DeviceInfo::fromJson(v.at("info"));
    if (v.has("files")) {
        const JsonValue& files = v.at("files");
        for (const auto& kv : files.items()) {
            FileMetadata f = FileMetadata::fromJson(kv.second);
            // La chiave dell'oggetto e' la fonte autorevole del fileId: usala
            // anche se "id" interno manca o diverge.
            if (f.id.empty()) f.id = kv.first;
            req.files.push_back(f);
        }
    }
    return req;
}

JsonValue buildPrepareUploadResponse(const std::string& sessionId,
                                     const std::map<std::string, std::string>& fileTokens) {
    JsonValue root = JsonValue::object();
    root["sessionId"] = sessionId;
    JsonValue files = JsonValue::object();
    for (const auto& kv : fileTokens) files[kv.first] = kv.second;
    root["files"] = files;
    return root;
}

} // namespace ls
