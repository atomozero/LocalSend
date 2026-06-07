// Genera un certificato self-signed e crea un contesto TLS server (SSL_CTX).
// Usato per rendere il server HTTPS come richiesto dal protocollo LocalSend.
#ifndef _LOCALSEND_TLS_CONTEXT_H
#define _LOCALSEND_TLS_CONTEXT_H

#include <string>

namespace LocalSend {

// Identita' TLS: contesto OpenSSL + fingerprint SHA-256 del certificato.
// Il fingerprint e' l'hash esadecimale del certificato in formato DER,
// come definito dal protocollo LocalSend v2.1.
struct TlsIdentity {
	void* ctx;                  // SSL_CTX* (void* per evitare include OpenSSL)
	std::string fingerprint;    // SHA-256 hex del certificato DER
};

// Crea un certificato self-signed RSA-2048, un SSL_CTX server configurato,
// e calcola il fingerprint. Ritorna ctx=nullptr se qualcosa fallisce.
TlsIdentity CreateSelfSignedTls(const std::string& commonName);

// Libera il contesto SSL.
void FreeTlsIdentity(TlsIdentity& identity);

} // namespace LocalSend

#endif // _LOCALSEND_TLS_CONTEXT_H
