#include "protocol/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace LocalSend {

namespace {

void
EscapeString(const std::string& s, std::string& out)
{
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


void
AppendUtf8(unsigned int cp, std::string& out)
{
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
	const std::string& fText;
	size_t fPos = 0;

	explicit Parser(const std::string& text) : fText(text) {}

	void SkipWhitespace()
	{
		while (fPos < fText.size()
			&& (fText[fPos] == ' ' || fText[fPos] == '\t'
				|| fText[fPos] == '\n' || fText[fPos] == '\r'))
			fPos++;
	}

	char Peek() const { return fPos < fText.size() ? fText[fPos] : '\0'; }

	[[noreturn]] void Error(const std::string& m) const
	{
		throw std::runtime_error("json parse: " + m + " a offset "
			+ std::to_string(fPos));
	}

	void ExpectLiteral(const char* lit)
	{
		for (const char* p = lit; *p; ++p) {
			if (fPos >= fText.size() || fText[fPos] != *p)
				Error(std::string("atteso '") + lit + "'");
			fPos++;
		}
	}

	unsigned int Hex4()
	{
		unsigned int v = 0;
		for (int k = 0; k < 4; ++k) {
			if (fPos >= fText.size())
				Error("escape \\u troncato");
			char c = fText[fPos++];
			v <<= 4;
			if (c >= '0' && c <= '9')
				v |= (c - '0');
			else if (c >= 'a' && c <= 'f')
				v |= (c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				v |= (c - 'A' + 10);
			else
				Error("cifra hex non valida");
		}
		return v;
	}

	std::string ParseString()
	{
		if (Peek() != '"')
			Error("attesa stringa");
		fPos++;
		std::string out;
		while (fPos < fText.size()) {
			char c = fText[fPos++];
			if (c == '"')
				return out;
			if (c == '\\') {
				if (fPos >= fText.size())
					Error("escape troncato");
				char e = fText[fPos++];
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
						unsigned int cp = Hex4();
						if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate
							if (fPos + 1 < fText.size() && fText[fPos] == '\\'
								&& fText[fPos + 1] == 'u') {
								fPos += 2;
								unsigned int lo = Hex4();
								cp = 0x10000 + ((cp - 0xD800) << 10)
									+ (lo - 0xDC00);
							}
						}
						AppendUtf8(cp, out);
						break;
					}
					default: Error("escape sconosciuto");
				}
			} else {
				out += c;
			}
		}
		Error("stringa non terminata");
	}

	JsonValue ParseNumber()
	{
		size_t start = fPos;
		if (Peek() == '-')
			fPos++;
		while (fPos < fText.size()) {
			char c = fText[fPos];
			if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
				|| c == '+' || c == '-')
				fPos++;
			else
				break;
		}
		std::string tok = fText.substr(start, fPos - start);
		if (tok.empty())
			Error("numero vuoto");
		return JsonValue(strtod(tok.c_str(), nullptr));
	}

	JsonValue ParseValue()
	{
		SkipWhitespace();
		char c = Peek();
		switch (c) {
			case '{': return ParseObject();
			case '[': return ParseArray();
			case '"': return JsonValue(ParseString());
			case 't': ExpectLiteral("true");  return JsonValue(true);
			case 'f': ExpectLiteral("false"); return JsonValue(false);
			case 'n': ExpectLiteral("null");  return JsonValue(nullptr);
			case '\0': Error("input vuoto");
			default:  return ParseNumber();
		}
	}

	JsonValue ParseObject()
	{
		JsonValue v = JsonValue::Object();
		fPos++; // '{'
		SkipWhitespace();
		if (Peek() == '}') {
			fPos++;
			return v;
		}
		for (;;) {
			SkipWhitespace();
			std::string key = ParseString();
			SkipWhitespace();
			if (Peek() != ':')
				Error("atteso ':'");
			fPos++;
			v[key] = ParseValue();
			SkipWhitespace();
			char c = Peek();
			if (c == ',') {
				fPos++;
				continue;
			}
			if (c == '}') {
				fPos++;
				break;
			}
			Error("atteso ',' o '}'");
		}
		return v;
	}

	JsonValue ParseArray()
	{
		JsonValue v = JsonValue::Array();
		fPos++; // '['
		SkipWhitespace();
		if (Peek() == ']') {
			fPos++;
			return v;
		}
		for (;;) {
			v.Add(ParseValue());
			SkipWhitespace();
			char c = Peek();
			if (c == ',') {
				fPos++;
				continue;
			}
			if (c == ']') {
				fPos++;
				break;
			}
			Error("atteso ',' o ']'");
		}
		return v;
	}
};

} // namespace


void
JsonValue::DumpTo(std::string& out) const
{
	switch (fType) {
		case Type::Null: out += "null"; break;
		case Type::Bool: out += (fBool ? "true" : "false"); break;
		case Type::Number: {
			if (std::isfinite(fNumber) && fNumber == std::floor(fNumber)
				&& fNumber >= -9.2e18 && fNumber <= 9.2e18) {
				char buf[32];
				snprintf(buf, sizeof(buf), "%lld",
					static_cast<long long>(fNumber));
				out += buf;
			} else {
				char buf[32];
				snprintf(buf, sizeof(buf), "%.17g", fNumber);
				out += buf;
			}
			break;
		}
		case Type::String: EscapeString(fString, out); break;
		case Type::Array: {
			out += '[';
			bool first = true;
			for (const auto& e : fArray) {
				if (!first)
					out += ',';
				first = false;
				e.DumpTo(out);
			}
			out += ']';
			break;
		}
		case Type::Object: {
			out += '{';
			bool first = true;
			for (const auto& kv : fObject) {
				if (!first)
					out += ',';
				first = false;
				EscapeString(kv.first, out);
				out += ':';
				kv.second.DumpTo(out);
			}
			out += '}';
			break;
		}
	}
}


std::string
JsonValue::Dump() const
{
	std::string out;
	DumpTo(out);
	return out;
}


JsonValue
JsonValue::Parse(const std::string& text)
{
	Parser p(text);
	JsonValue v = p.ParseValue();
	p.SkipWhitespace();
	return v;
}

} // namespace LocalSend
