// Verifica OpenSSL su Haiku: versione + SHA-256 (serve a sha256 file e fingerprint).
// Prima accertare gli header:  pkgman install openssl_devel
// Compila:  gcc -o test_openssl test_openssl.cpp -lcrypto
// Successo = stampa la versione OpenSSL e l'hash atteso di "abc".
//
// SHA-256("abc") atteso:
//   ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad

#include <openssl/sha.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstring>

int main() {
    printf("OpenSSL header version: %s\n", OPENSSL_VERSION_TEXT);

    const char* data = "abc";
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)data, strlen(data), hash);

    printf("SHA-256(\"abc\") = ");
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) printf("%02x", hash[i]);
    printf("\n");
    printf("atteso         = "
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
    return 0;
}
