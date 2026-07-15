// Interfaccia del server HTTP, DEFINITA in L1-prep e IMPLEMENTATA in L1.
// Separata da IHttpClient di proposito: L0 usa solo il client; L1 aggiunge un
// SocketHttpServer (socket BSD) che realizza questa interfaccia senza toccare
// il mittente. La logica del ricevente (ReceiveSession) dipende da
// questa astrazione, non dai socket: cosi' e' testabile senza rete.
#ifndef _LOCALSEND_IHTTP_SERVER_H
#define _LOCALSEND_IHTTP_SERVER_H

#include <functional>
#include <map>
#include <string>

namespace LocalSend {

// Richiesta HTTP in ingresso, come la vede un handler del server.
struct HttpRequest {
	std::string method;                         // "POST", "GET", ...
	std::string path;                           // solo il path, senza query
	std::map<std::string, std::string> query;   // chiavi/valori decodificati
	std::map<std::string, std::string> headers; // chiavi in minuscolo
	std::string body;
	// Indirizzo IP del client (es. "192.168.1.42"). Serve al handler di
	// /api/localsend/v2/register per aggiungere il peer al proprio elenco:
	// il payload della POST non contiene l'IP del mittente (spesso il peer
	// non lo conosce), quindi va preso dal socket TCP.
	std::string clientHost;

	// Valore di un parametro di query, o "" se assente.
	std::string Query(const std::string& key) const
	{
		auto it = query.find(key);
		return it == query.end() ? std::string() : it->second;
	}
};

// Risposta che un handler restituisce al server.
struct HttpServerResponse {
	int status = 200;
	std::string contentType = "application/json";
	std::string body;
	std::string extraHeaders; // header aggiuntivi (es. Content-Disposition)

	static HttpServerResponse Json(int status, const std::string& body)
	{
		return HttpServerResponse{status, "application/json", body, ""};
	}
	static HttpServerResponse Empty(int status)
	{
		return HttpServerResponse{status, "text/plain", "", ""};
	}
};

class IHttpServer {
public:
	virtual ~IHttpServer() {}

	typedef std::function<HttpServerResponse(const HttpRequest&)> Handler;

	// Registra un handler per (metodo, path). Il path NON include la query.
	virtual void Route(const std::string& method, const std::string& path,
		Handler handler) = 0;

	// Prepara l'ascolto sulla porta (socket/bind/listen). Ritorna false se il
	// bind fallisce (es. porta occupata). Non blocca.
	virtual bool Start(int port) = 0;

	// Entra nel loop di accept e serve le richieste finche' Stop() non e'
	// chiamato (da un altro thread o da un gestore di segnale). Bloccante.
	virtual void Run() = 0;

	// Ferma il loop e libera la porta.
	virtual void Stop() = 0;
};

// Decodifica percentuale (inverso di UrlEncode lato client). Portabile: serve
// sia al parsing della query sia, in L1, alla lettura dei path.
std::string UrlDecode(const std::string& s);

// Spezza una query string ("a=1&b=2", senza il '?') in coppie decodificate.
std::map<std::string, std::string> ParseQuery(const std::string& queryString);

// Decide l'accesso al download API (bacheca) come codice HTTP: 0 = ammesso,
// 401 = PIN richiesto/errato, 403 = rifiutato. L'identita' (gia' autorizzata a
// monte) e' la via principale; se non lo e', un PIN configurato lo autentica in
// alternativa (conforme al PIN del protocollo). Logica pura, testabile.
int DownloadAccessStatus(bool identityAllowed, const std::string& configuredPin,
	const std::string& queryPin);

} // namespace LocalSend

#endif // _LOCALSEND_IHTTP_SERVER_H
