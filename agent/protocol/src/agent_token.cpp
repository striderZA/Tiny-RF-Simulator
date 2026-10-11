#include "agent_token.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <optional>
#include <string>

#if defined(_WIN32)
#include <windows.h>

#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <sys/types.h>
#endif

std::optional<std::string> generateAgentToken() {
    constexpr std::size_t kRandomBytes = 32;
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::array<unsigned char, kRandomBytes> random_bytes{};

#if defined(_WIN32)
    const NTSTATUS status =
        BCryptGenRandom(nullptr, random_bytes.data(), static_cast<ULONG>(random_bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        return std::nullopt;
    }
#elif defined(__linux__)
    std::size_t bytes_read = 0;
    while (bytes_read < random_bytes.size()) {
        const ssize_t result =
            getrandom(random_bytes.data() + bytes_read, random_bytes.size() - bytes_read, 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        if (result == 0) {
            return std::nullopt;
        }
        bytes_read += static_cast<std::size_t>(result);
    }
#else
    return std::nullopt;
#endif

    std::string token;
    token.reserve(random_bytes.size() * 2);
    for (const unsigned char byte : random_bytes) {
        token.push_back(kHexDigits[byte >> 4]);
        token.push_back(kHexDigits[byte & 0x0f]);
    }
    return token;
}

bool agentTokensEqual(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference = static_cast<unsigned char>(
            difference | static_cast<unsigned char>(left[index] ^ right[index]));
    }
    return difference == 0;
}
