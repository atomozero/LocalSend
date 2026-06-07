// Fingerprint del device. In HTTP (L0) e' una stringa casuale, generata una
// volta e ricordata. In HTTPS (L3) sara' lo SHA-256 del certificato.
#ifndef _LOCALSEND_FINGERPRINT_H
#define _LOCALSEND_FINGERPRINT_H

#include <string>

namespace LocalSend {

// Genera una nuova stringa casuale esadecimale.
std::string GenerateFingerprint();

// Carica il fingerprint dal file indicato; se assente o vuoto, ne genera uno
// nuovo e lo salva. Ritorna la stringa usata.
std::string LoadOrCreateFingerprint(const std::string& path);

} // namespace LocalSend

#endif // _LOCALSEND_FINGERPRINT_H
