// Supporto portabile per IHttpServer: decodifica percentuale e parsing della
// query string. Niente socket, niente BeAPI: pura manipolazione di stringhe,
// quindi compilabile e testabile su qualunque host (non solo Haiku).
#include "net/IHttpServer.h"

#include <cctype>

namespace LocalSend {

namespace {

int
HexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

} // namespace


std::string
UrlDecode(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		char c = s[i];
		if (c == '+') {
			out += ' ';
		} else if (c == '%' && i + 2 < s.size()) {
			int hi = HexValue(s[i + 1]);
			int lo = HexValue(s[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out += static_cast<char>((hi << 4) | lo);
				i += 2;
			} else {
				out += c; // sequenza malformata: lascia il '%' cosi' com'e'
			}
		} else {
			out += c;
		}
	}
	return out;
}


std::map<std::string, std::string>
ParseQuery(const std::string& queryString)
{
	std::map<std::string, std::string> out;
	size_t i = 0;
	while (i < queryString.size()) {
		size_t amp = queryString.find('&', i);
		if (amp == std::string::npos)
			amp = queryString.size();
		std::string pair = queryString.substr(i, amp - i);
		if (!pair.empty()) {
			size_t eq = pair.find('=');
			if (eq == std::string::npos) {
				out[UrlDecode(pair)] = "";
			} else {
				out[UrlDecode(pair.substr(0, eq))]
					= UrlDecode(pair.substr(eq + 1));
			}
		}
		i = amp + 1;
	}
	return out;
}


int
DownloadAccessStatus(bool identityAllowed, const std::string& configuredPin,
	const std::string& queryPin)
{
	if (identityAllowed)
		return 0; // gia' autorizzato per identita': nessun PIN richiesto
	if (!configuredPin.empty())
		return queryPin == configuredPin ? 0 : 401; // PIN richiesto/errato
	return 403; // nessuna identita', nessun PIN configurato: rifiutato
}

} // namespace LocalSend
