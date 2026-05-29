#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

class SessionState {
  public:
    SessionState() {
#ifdef _WIN32
        char buf[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) == 0) {
            m_path = "app.ini";
            return;
        }
        m_path = buf;
        auto pos = m_path.find_last_of('\\');
        if (pos != std::string::npos)
            m_path = m_path.substr(0, pos + 1) + "app.ini";
        else
            m_path = "app.ini";
#else
        m_path = "app.ini";
#endif
    }

    void save(const char* section, const char* key, const char* value) {
#ifdef _WIN32
        WritePrivateProfileStringA(section, key, value, m_path.c_str());
#endif
    }

    std::string load(const char* section, const char* key, const char* default_val) const {
#ifdef _WIN32
        std::vector<char> buf(256);
        DWORD ret;
        do {
            ret = GetPrivateProfileStringA(section, key, default_val, buf.data(),
                                           static_cast<DWORD>(buf.size()), m_path.c_str());
            if (ret == buf.size() - 1 && buf.size() < 32768)
                buf.resize(buf.size() * 2);
            else
                break;
        } while (true);
        return buf.data();
#else
        (void)section;
        (void)key;
        return std::string(default_val);
#endif
    }

    bool loadBool(const char* section, const char* key, bool default_val) const {
        auto s = load(section, key, default_val ? "1" : "0");
        return s == "1";
    }

    void saveBool(const char* section, const char* key, bool val) {
        save(section, key, val ? "1" : "0");
    }

    bool fileExists() const {
#ifdef _WIN32
        return GetFileAttributesA(m_path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
        return false;
#endif
    }

  private:
    std::string m_path;
};
