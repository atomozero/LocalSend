// Modulo JSON minimale e self-contained (niente dipendenze esterne).
// Sufficiente per il protocollo LocalSend: genera le richieste e legge le
// risposte (sessionId + mappa token, messaggi di errore).
// Sostituibile in futuro con nlohmann/json senza toccare il resto: i Models
// usano solo questa interfaccia.
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ls {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() : type_(Type::Null) {}
    JsonValue(std::nullptr_t) : type_(Type::Null) {}
    JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    JsonValue(int n) : type_(Type::Number), num_(n) {}
    JsonValue(long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    JsonValue(long long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    JsonValue(double n) : type_(Type::Number), num_(n) {}
    JsonValue(const char* s) : type_(Type::String), str_(s) {}
    JsonValue(const std::string& s) : type_(Type::String), str_(s) {}

    static JsonValue object() { JsonValue v; v.type_ = Type::Object; return v; }
    static JsonValue array()  { JsonValue v; v.type_ = Type::Array;  return v; }

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray()  const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }

    // Accesso/costruzione oggetto.
    JsonValue& operator[](const std::string& key) {
        type_ = Type::Object;
        return obj_[key];
    }
    bool has(const std::string& key) const {
        return type_ == Type::Object && obj_.find(key) != obj_.end();
    }
    const JsonValue& at(const std::string& key) const {
        auto it = obj_.find(key);
        if (it == obj_.end()) throw std::runtime_error("json: chiave assente: " + key);
        return it->second;
    }
    const std::map<std::string, JsonValue>& items() const { return obj_; }

    // Array.
    void push_back(const JsonValue& v) { type_ = Type::Array; arr_.push_back(v); }
    const std::vector<JsonValue>& elements() const { return arr_; }

    // Lettura scalari con default.
    std::string asString(const std::string& def = "") const {
        return type_ == Type::String ? str_ : def;
    }
    double asNumber(double def = 0) const {
        return type_ == Type::Number ? num_ : def;
    }
    long long asInt(long long def = 0) const {
        return type_ == Type::Number ? static_cast<long long>(num_) : def;
    }
    bool asBool(bool def = false) const {
        return type_ == Type::Bool ? bool_ : def;
    }

    std::string dump() const;
    static JsonValue parse(const std::string& text);

private:
    Type type_;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<JsonValue> arr_;
    std::map<std::string, JsonValue> obj_;

    void dumpTo(std::string& out) const;
};

} // namespace ls
