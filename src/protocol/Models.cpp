#include "protocol/Models.h"

namespace LocalSend {

JsonValue
DeviceInfo::ToJson() const
{
	JsonValue v = JsonValue::Object();
	v["alias"] = alias;
	v["version"] = version;
	v["deviceModel"] = deviceModel;
	v["deviceType"] = deviceType;
	v["fingerprint"] = fingerprint;
	v["port"] = port;
	v["protocol"] = protocol;
	v["download"] = download;
	return v;
}


DeviceInfo
DeviceInfo::FromJson(const JsonValue& v)
{
	DeviceInfo d;
	if (v.Has("alias"))
		d.alias = v.At("alias").AsString(d.alias);
	if (v.Has("version"))
		d.version = v.At("version").AsString(d.version);
	if (v.Has("deviceModel"))
		d.deviceModel = v.At("deviceModel").AsString(d.deviceModel);
	if (v.Has("deviceType"))
		d.deviceType = v.At("deviceType").AsString(d.deviceType);
	if (v.Has("fingerprint"))
		d.fingerprint = v.At("fingerprint").AsString();
	if (v.Has("port"))
		d.port = static_cast<int>(v.At("port").AsInt(d.port));
	if (v.Has("protocol"))
		d.protocol = v.At("protocol").AsString(d.protocol);
	if (v.Has("download"))
		d.download = v.At("download").AsBool(d.download);
	return d;
}


JsonValue
FileMetadata::ToJson() const
{
	JsonValue v = JsonValue::Object();
	v["id"] = id;
	v["fileName"] = fileName;
	v["size"] = size;
	v["fileType"] = fileType;
	v["sha256"] = sha256.empty() ? JsonValue(nullptr) : JsonValue(sha256);
	v["preview"] = preview.empty() ? JsonValue(nullptr) : JsonValue(preview);

	if (!modified.empty() || !accessed.empty()) {
		JsonValue meta = JsonValue::Object();
		if (!modified.empty())
			meta["modified"] = modified;
		if (!accessed.empty())
			meta["accessed"] = accessed;
		v["metadata"] = meta;
	}
	return v;
}


FileMetadata
FileMetadata::FromJson(const JsonValue& v)
{
	FileMetadata f;
	if (v.Has("id"))
		f.id = v.At("id").AsString();
	if (v.Has("fileName"))
		f.fileName = v.At("fileName").AsString();
	if (v.Has("size"))
		f.size = v.At("size").AsInt();
	if (v.Has("fileType"))
		f.fileType = v.At("fileType").AsString(f.fileType);
	if (v.Has("sha256") && v.At("sha256").IsString())
		f.sha256 = v.At("sha256").AsString();
	if (v.Has("preview") && v.At("preview").IsString())
		f.preview = v.At("preview").AsString();
	if (v.Has("metadata")) {
		const JsonValue& m = v.At("metadata");
		if (m.Has("modified"))
			f.modified = m.At("modified").AsString();
		if (m.Has("accessed"))
			f.accessed = m.At("accessed").AsString();
	}
	return f;
}


JsonValue
BuildPrepareUpload(const DeviceInfo& info,
	const std::vector<FileMetadata>& files)
{
	JsonValue root = JsonValue::Object();
	root["info"] = info.ToJson();

	JsonValue filesObj = JsonValue::Object();
	for (const auto& f : files)
		filesObj[f.id] = f.ToJson();
	root["files"] = filesObj;
	return root;
}


PrepareUploadResult
ParsePrepareUploadResponse(const std::string& body)
{
	PrepareUploadResult r;
	JsonValue v = JsonValue::Parse(body);
	if (v.Has("sessionId"))
		r.sessionId = v.At("sessionId").AsString();
	if (v.Has("files")) {
		const JsonValue& files = v.At("files");
		for (const auto& kv : files.Items())
			r.fileTokens[kv.first] = kv.second.AsString();
	}
	return r;
}


IncomingPrepareUpload
ParsePrepareUploadRequest(const std::string& body)
{
	IncomingPrepareUpload req;
	JsonValue v = JsonValue::Parse(body);
	if (v.Has("info"))
		req.sender = DeviceInfo::FromJson(v.At("info"));
	if (v.Has("files")) {
		const JsonValue& files = v.At("files");
		for (const auto& kv : files.Items()) {
			FileMetadata f = FileMetadata::FromJson(kv.second);
			// La chiave dell'oggetto e' la fonte autorevole del fileId: usala
			// anche se "id" interno manca o diverge.
			if (f.id.empty())
				f.id = kv.first;
			req.files.push_back(f);
		}
	}
	return req;
}


JsonValue
BuildPrepareUploadResponse(const std::string& sessionId,
	const std::map<std::string, std::string>& fileTokens)
{
	JsonValue root = JsonValue::Object();
	root["sessionId"] = sessionId;
	JsonValue files = JsonValue::Object();
	for (const auto& kv : fileTokens)
		files[kv.first] = kv.second;
	root["files"] = files;
	return root;
}

} // namespace LocalSend
