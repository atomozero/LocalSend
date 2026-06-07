// Genera un certificato self-signed e un contesto TLS server con OpenSSL 3.

#include "net/TlsContext.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <cstdio>
#include <cstring>

namespace LocalSend {

namespace {

std::string
Sha256Hex(const unsigned char* data, int len)
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256(data, static_cast<size_t>(len), hash);

	static const char* hex = "0123456789abcdef";
	std::string out;
	out.reserve(SHA256_DIGEST_LENGTH * 2);
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		out += hex[hash[i] >> 4];
		out += hex[hash[i] & 0x0f];
	}
	return out;
}

} // namespace


TlsIdentity
CreateSelfSignedTls(const std::string& commonName)
{
	TlsIdentity id{nullptr, ""};

	// Genera chiave RSA-2048.
	EVP_PKEY* pkey = EVP_RSA_gen(2048);
	if (!pkey) {
		fprintf(stderr, "TLS: generazione chiave RSA fallita\n");
		return id;
	}

	// Crea certificato X509.
	X509* cert = X509_new();
	if (!cert) {
		EVP_PKEY_free(pkey);
		return id;
	}

	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_get_notBefore(cert), 0);
	X509_gmtime_adj(X509_get_notAfter(cert), 365L * 24 * 3600 * 10);
	X509_set_pubkey(cert, pkey);

	X509_NAME* name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
		(const unsigned char*)commonName.c_str(), -1, -1, 0);
	X509_set_issuer_name(cert, name);

	if (!X509_sign(cert, pkey, EVP_sha256())) {
		fprintf(stderr, "TLS: firma del certificato fallita\n");
		X509_free(cert);
		EVP_PKEY_free(pkey);
		return id;
	}

	// Fingerprint: SHA-256 del certificato in formato DER.
	unsigned char* der = nullptr;
	int derLen = i2d_X509(cert, &der);
	if (derLen > 0 && der) {
		id.fingerprint = Sha256Hex(der, derLen);
		OPENSSL_free(der);
	}

	// Crea SSL_CTX server.
	SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
	if (!ctx) {
		fprintf(stderr, "TLS: creazione SSL_CTX fallita\n");
		X509_free(cert);
		EVP_PKEY_free(pkey);
		id.fingerprint.clear();
		return id;
	}

	SSL_CTX_use_certificate(ctx, cert);
	SSL_CTX_use_PrivateKey(ctx, pkey);

	X509_free(cert);
	EVP_PKEY_free(pkey);

	id.ctx = ctx;
	return id;
}


void
FreeTlsIdentity(TlsIdentity& identity)
{
	if (identity.ctx) {
		SSL_CTX_free(static_cast<SSL_CTX*>(identity.ctx));
		identity.ctx = nullptr;
	}
}

} // namespace LocalSend
