// Modulo JSON minimale e self-contained (niente dipendenze esterne).
// Sufficiente per il protocollo LocalSend: genera le richieste e legge le
// risposte (sessionId + mappa token, messaggi di errore).
// Sostituibile in futuro con nlohmann/json senza toccare il resto: i Models
// usano solo questa interfaccia.
#ifndef _LOCALSEND_JSON_H
#define _LOCALSEND_JSON_H

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace LocalSend {

class JsonValue {
public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	JsonValue() : fType(Type::Null) {}
	JsonValue(std::nullptr_t) : fType(Type::Null) {}
	JsonValue(bool b) : fType(Type::Bool), fBool(b) {}
	JsonValue(int n) : fType(Type::Number), fNumber(n) {}
	JsonValue(long n) : fType(Type::Number), fNumber(static_cast<double>(n)) {}
	JsonValue(long long n)
		: fType(Type::Number), fNumber(static_cast<double>(n)) {}
	JsonValue(double n) : fType(Type::Number), fNumber(n) {}
	JsonValue(const char* s) : fType(Type::String), fString(s) {}
	JsonValue(const std::string& s) : fType(Type::String), fString(s) {}

	static JsonValue Object() { JsonValue v; v.fType = Type::Object; return v; }
	static JsonValue Array()  { JsonValue v; v.fType = Type::Array;  return v; }

	Type GetType() const { return fType; }
	bool IsNull()   const { return fType == Type::Null; }
	bool IsObject() const { return fType == Type::Object; }
	bool IsArray()  const { return fType == Type::Array; }
	bool IsString() const { return fType == Type::String; }

	// Accesso/costruzione oggetto.
	JsonValue& operator[](const std::string& key)
	{
		fType = Type::Object;
		return fObject[key];
	}
	bool Has(const std::string& key) const
	{
		return fType == Type::Object && fObject.find(key) != fObject.end();
	}
	const JsonValue& At(const std::string& key) const
	{
		auto it = fObject.find(key);
		if (it == fObject.end())
			throw std::runtime_error("json: chiave assente: " + key);
		return it->second;
	}
	const std::map<std::string, JsonValue>& Items() const { return fObject; }

	// Array.
	void Add(const JsonValue& v) { fType = Type::Array; fArray.push_back(v); }
	const std::vector<JsonValue>& Elements() const { return fArray; }

	// Lettura scalari con default.
	std::string AsString(const std::string& def = "") const
	{
		return fType == Type::String ? fString : def;
	}
	double AsNumber(double def = 0) const
	{
		return fType == Type::Number ? fNumber : def;
	}
	long long AsInt(long long def = 0) const
	{
		return fType == Type::Number ? static_cast<long long>(fNumber) : def;
	}
	bool AsBool(bool def = false) const
	{
		return fType == Type::Bool ? fBool : def;
	}

	std::string Dump() const;
	static JsonValue Parse(const std::string& text);

private:
	Type fType;
	bool fBool = false;
	double fNumber = 0;
	std::string fString;
	std::vector<JsonValue> fArray;
	std::map<std::string, JsonValue> fObject;

	void DumpTo(std::string& out) const;
};

} // namespace LocalSend

#endif // _LOCALSEND_JSON_H
