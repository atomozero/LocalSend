// Da un percorso su disco costruisce i FileMetadata del protocollo: nome, size,
// MIME, tempi modified/accessed in ISO 8601. Il MIME qui e' basato
// sull'estensione (portabile); il MIME nativo Haiku (BNodeInfo) e' un
// miglioramento previsto in L4.
#pragma once

#include <string>

#include "protocol/Models.h"

namespace ls {

// Costruisce i metadati per il file in 'localPath' con id assegnato.
// Ritorna false se il file non e' leggibile o non e' un file regolare.
bool buildFileMetadata(const std::string& localPath, const std::string& id,
                       FileMetadata& out);

// MIME indovinato dall'estensione, fallback application/octet-stream.
std::string guessMimeType(const std::string& fileName);

} // namespace ls
