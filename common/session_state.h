#pragma once

#include <string>
#include <windows.h>

class SessionState {
  public:
    SessionState() {
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
    }

    void save(const char* section, const char* key, const char* value) {
        WritePrivateProfileStringA(section, key, value, m_path.c_str());
    }

    std::string load(const char* section, const char* key, const char* default_val) const {
        char buf[256];
        GetPrivateProfileStringA(section, key, default_val, buf, sizeof(buf), m_path.c_str());
        return buf;
    }

    bool loadBool(const char* section, const char* key, bool default_val) const {
        auto s = load(section, key, default_val ? "1" : "0");
        return s == "1";
    }

    void saveBool(const char* section, const char* key, bool val) {
        save(section, key, val ? "1" : "0");
    }

    bool fileExists() const {
        return GetFileAttributesA(m_path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

private:
    std::string m_path;
};
