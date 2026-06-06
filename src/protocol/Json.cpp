#include "protocol/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ls {

namespace {

void escapeString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void appendUtf8(unsigned int cp, std::string& out) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& str) : s(str) {}

    void ws() {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            i++;
    }
    char peek() const { return i < s.size() ? s[i] : '\0'; }

    [[noreturn]] void err(const std::string& m) const {
        throw std::runtime_error("json parse: " + m + " a offset " +
                                 std::to_string(i));
    }

    void expectLit(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (i >= s.size() || s[i] != *p) err(std::string("atteso '") + lit + "'");
            i++;
        }
    }

    unsigned int hex4() {
        unsigned int v = 0;
        for (int k = 0; k < 4; ++k) {
            if (i >= s.size()) err("escape \\u troncato");
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
            else err("cifra hex non valida");
        }
        return v;
    }

    std::string parseString() {
        if (peek() != '"') err("attesa stringa");
        i++;
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) err("escape troncato");
                char e = s[i++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        unsigned int cp = hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate
                            if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                i += 2;
                                unsigned int lo = hex4();
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                        }
                        appendUtf8(cp, out);
                        break;
                    }
                    default: err("escape sconosciuto");
                }
            } else {
                out += c;
            }
        }
        err("stringa non terminata");
    }

    JsonValue parseNumber() {
        size_t start = i;
        if (peek() == '-') i++;
        while (i < s.size()) {
            char c = s[i];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-')
                i++;
            else
                break;
        }
        std::string tok = s.substr(start, i - start);
        if (tok.empty()) err("numero vuoto");
        return JsonValue(strtod(tok.c_str(), nullptr));
    }

    JsonValue parseValue() {
        ws();
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return JsonValue(parseString());
            case 't': expectLit("true");  return JsonValue(true);
            case 'f': expectLit("false"); return JsonValue(false);
            case 'n': expectLit("null");  return JsonValue(nullptr);
            case '\0': err("input vuoto");
            default:  return parseNumber();
        }
    }

    JsonValue parseObject() {
        JsonValue v = JsonValue::object();
        i++; // '{'
        ws();
        if (peek() == '}') { i++; return v; }
        for (;;) {
            ws();
            std::string key = parseString();
            ws();
            if (peek() != ':') err("atteso ':'");
            i++;
            v[key] = parseValue();
            ws();
            char c = peek();
            if (c == ',') { i++; continue; }
            if (c == '}') { i++; break; }
            err("atteso ',' o '}'");
        }
        return v;
    }

    JsonValue parseArray() {
        JsonValue v = JsonValue::array();
        i++; // '['
        ws();
        if (peek() == ']') { i++; return v; }
        for (;;) {
            v.push_back(parseValue());
            ws();
            char c = peek();
            if (c == ',') { i++; continue; }
            if (c == ']') { i++; break; }
            err("atteso ',' o ']'");
        }
        return v;
    }
};

} // namespace

void JsonValue::dumpTo(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += (bool_ ? "true" : "false"); break;
        case Type::Number: {
            if (std::isfinite(num_) && num_ == std::floor(num_) &&
                num_ >= -9.2e18 && num_ <= 9.2e18) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(num_));
                out += buf;
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.17g", num_);
                out += buf;
            }
            break;
        }
        case Type::String: escapeString(str_, out); break;
        case Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& e : arr_) {
                if (!first) out += ',';
                first = false;
                e.dumpTo(out);
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : obj_) {
                if (!first) out += ',';
                first = false;
                escapeString(kv.first, out);
                out += ':';
                kv.second.dumpTo(out);
            }
            out += '}';
            break;
        }
    }
}

std::string JsonValue::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

JsonValue JsonValue::parse(const std::string& text) {
    Parser p(text);
    JsonValue v = p.parseValue();
    p.ws();
    return v;
}

} // namespace ls
