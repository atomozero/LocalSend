// Costanti del protocollo LocalSend v2.1. Unico posto dove vivono porte e path.
// Condiviso tra mittente (L0) e futuro ricevente (L1).
#pragma once

namespace ls {

constexpr const char* kMulticastGroup   = "224.0.0.167";
constexpr int         kDefaultPort      = 53317;
constexpr const char* kProtocolVersion  = "2.1";

constexpr const char* kApiPrepareUpload = "/api/localsend/v2/prepare-upload";
constexpr const char* kApiUpload        = "/api/localsend/v2/upload";
constexpr const char* kApiCancel        = "/api/localsend/v2/cancel";
constexpr const char* kApiRegister      = "/api/localsend/v2/register";

} // namespace ls
