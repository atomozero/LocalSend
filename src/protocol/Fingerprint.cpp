#include "protocol/Fingerprint.h"

#include <cstdio>
#include <fstream>
#include <random>

namespace ls {

std::string generateFingerprint() {
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd());
    std::uniform_int_distribution<int> dist(0, 15);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int k = 0; k < 64; ++k) out += hex[dist(gen)];
    return out;
}

std::string loadOrCreateFingerprint(const std::string& path) {
    {
        std::ifstream in(path);
        if (in) {
            std::string fp;
            std::getline(in, fp);
            if (!fp.empty()) return fp;
        }
    }
    std::string fp = generateFingerprint();
    std::ofstream out(path);
    if (out) out << fp << "\n";
    return fp;
}

} // namespace ls
