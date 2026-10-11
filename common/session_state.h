#pragma once

#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

class SessionState {
  public:
    SessionState() : m_path(defaultPath()) {}
    explicit SessionState(std::filesystem::path path) : m_path(std::move(path)) {}

    void save(const char *section, const char *key, const char *value) {
#ifdef _WIN32
        const auto wide_section = acpToWide(section);
        const auto wide_key = acpToWide(key);
        const auto wide_value = acpToWide(value);
        WritePrivateProfileStringW(wide_section ? wide_section->c_str() : nullptr,
                                   wide_key ? wide_key->c_str() : nullptr,
                                   wide_value ? wide_value->c_str() : nullptr, m_path.c_str());
#endif
    }

    std::string load(const char *section, const char *key, const char *default_val) const {
#ifdef _WIN32
        const auto wide_section = acpToWide(section);
        const auto wide_key = acpToWide(key);
        const auto wide_default = acpToWide(default_val);
        std::vector<wchar_t> buf(256);
        DWORD ret;
        do {
            ret =
                GetPrivateProfileStringW(wide_section ? wide_section->c_str() : nullptr,
                                         wide_key ? wide_key->c_str() : nullptr,
                                         wide_default ? wide_default->c_str() : nullptr, buf.data(),
                                         static_cast<DWORD>(buf.size()), m_path.c_str());
            if (ret == buf.size() - 1 && buf.size() < 32768)
                buf.resize(buf.size() * 2);
            else
                break;
        } while (true);
        return wideToAcp(buf.data());
#else
        (void)section;
        (void)key;
        return std::string(default_val);
#endif
    }

    bool loadBool(const char *section, const char *key, bool default_val) const {
        auto s = load(section, key, default_val ? "1" : "0");
        return s == "1";
    }

    void saveBool(const char *section, const char *key, bool val) {
        save(section, key, val ? "1" : "0");
    }

    bool fileExists() const {
#ifdef _WIN32
        return GetFileAttributesW(m_path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
        return false;
#endif
    }

  private:
#ifdef _WIN32
    static std::optional<std::wstring> acpToWide(const char *text) {
        if (text == nullptr)
            return std::nullopt;

        const std::string_view narrow(text);
        if (narrow.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            throw std::length_error("SessionState ACP text is too long");
        if (narrow.empty())
            return std::wstring{};

        const int required = MultiByteToWideChar(CP_ACP, 0, narrow.data(),
                                                 static_cast<int>(narrow.size()), nullptr, 0);
        if (required == 0) {
            const DWORD error = GetLastError();
            throw std::system_error(error, std::system_category(),
                                    "SessionState ACP to UTF-16 conversion");
        }

        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        const int converted = MultiByteToWideChar(
            CP_ACP, 0, narrow.data(), static_cast<int>(narrow.size()), wide.data(), required);
        if (converted != required) {
            const DWORD error = GetLastError();
            throw std::system_error(error, std::system_category(),
                                    "SessionState ACP to UTF-16 conversion");
        }
        return wide;
    }

    static std::string wideToAcp(std::wstring_view wide) {
        if (wide.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            throw std::length_error("SessionState UTF-16 text is too long");
        if (wide.empty())
            return {};

        const int required = WideCharToMultiByte(
            CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (required == 0) {
            const DWORD error = GetLastError();
            throw std::system_error(error, std::system_category(),
                                    "SessionState UTF-16 to ACP conversion");
        }

        std::string narrow(static_cast<std::size_t>(required), '\0');
        const int converted =
            WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()),
                                narrow.data(), required, nullptr, nullptr);
        if (converted != required) {
            const DWORD error = GetLastError();
            throw std::system_error(error, std::system_category(),
                                    "SessionState UTF-16 to ACP conversion");
        }
        return narrow;
    }
#endif

    static std::filesystem::path defaultPath() {
#ifdef _WIN32
        constexpr std::size_t maxModulePathChars = 32768;
        std::vector<wchar_t> buffer(256);
        while (true) {
            const DWORD length =
                GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return std::filesystem::path(L"app.ini");
            if (length < buffer.size()) {
                const std::filesystem::path executable(
                    std::wstring(buffer.data(), static_cast<std::size_t>(length)));
                return executable.parent_path() / L"app.ini";
            }
            if (buffer.size() >= maxModulePathChars)
                break;
            const std::size_t next_size =
                buffer.size() > maxModulePathChars / 2 ? maxModulePathChars : buffer.size() * 2;
            buffer.resize(next_size);
        }
        return std::filesystem::path(L"app.ini");
#else
        return std::filesystem::path("app.ini");
#endif
    }

    std::filesystem::path m_path;
};
