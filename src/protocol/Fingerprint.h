// Fingerprint del device. In modalita' HTTP (L0) e' una stringa casuale, generata
// una volta e ricordata. In HTTPS (L3) sara' lo SHA-256 del certificato.
#pragma once

#include <string>

namespace ls {

// Genera una nuova stringa casuale esadecimale.
std::string generateFingerprint();

// Carica il fingerprint dal file indicato; se assente o vuoto, ne genera uno
// nuovo e lo salva. Ritorna la stringa usata.
std::string loadOrCreateFingerprint(const std::string& path);

} // namespace ls
