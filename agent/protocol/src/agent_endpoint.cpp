#include "agent_endpoint.h"

#include "agent_token.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view kEndpointProtocol = "rfsim-agent/1";
constexpr std::size_t kMaxEndpointBytes = 64 * 1024;
constexpr std::size_t kMaxVersionBytes = 256;
constexpr std::array<std::string_view, 7> kEndpointKeys{
    "protocol", "port", "bridge_token", "gui_token", "pid", "app_version", "catalog_version"};

void setError(std::string *error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool isLowerHexToken(std::string_view token) {
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool isValidEndpoint(const AgentEndpoint &endpoint, std::string *error) {
    if (endpoint.port < 1 || endpoint.port > 65535) {
        setError(error, "endpoint port is outside 1..65535");
        return false;
    }
    if (!isLowerHexToken(endpoint.bridge_token) || !isLowerHexToken(endpoint.gui_token)) {
        setError(error, "endpoint tokens must be 64 lowercase hexadecimal characters");
        return false;
    }
    if (endpoint.pid <= 0) {
        setError(error, "endpoint pid must be positive");
        return false;
    }
    if (endpoint.app_version.empty() || endpoint.app_version.size() > kMaxVersionBytes) {
        setError(error, "endpoint app_version must contain 1..256 bytes");
        return false;
    }
    if (endpoint.catalog_version < 1) {
        setError(error, "endpoint catalog_version must be positive");
        return false;
    }
    return true;
}

nlohmann::json endpointJson(const AgentEndpoint &endpoint) {
    return nlohmann::json{{"protocol", std::string(kEndpointProtocol)},
                          {"port", endpoint.port},
                          {"bridge_token", endpoint.bridge_token},
                          {"gui_token", endpoint.gui_token},
                          {"pid", endpoint.pid},
                          {"app_version", endpoint.app_version},
                          {"catalog_version", endpoint.catalog_version}};
}

bool parseInteger(const nlohmann::json &value, long long minimum, long long maximum,
                  long long &result) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(maximum)) {
            return false;
        }
        result = static_cast<long long>(number);
    } else if (value.is_number_integer()) {
        result = value.get<long long>();
    } else {
        return false;
    }
    return result >= minimum && result <= maximum;
}

std::optional<AgentEndpoint> parseEndpoint(std::string_view contents, std::string *error) {
    if (contents.empty() || contents.size() > kMaxEndpointBytes) {
        setError(error, "endpoint file size is invalid");
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(contents.begin(), contents.end());
        if (!json.is_object() || json.size() != kEndpointKeys.size()) {
            setError(error, "endpoint JSON must contain exactly seven fields");
            return std::nullopt;
        }
        for (const auto key : kEndpointKeys) {
            if (!json.contains(std::string(key))) {
                setError(error, "endpoint JSON is missing a required field");
                return std::nullopt;
            }
        }
        if (!json.at("protocol").is_string() ||
            json.at("protocol").get<std::string>() != std::string(kEndpointProtocol)) {
            setError(error, "endpoint protocol is unsupported");
            return std::nullopt;
        }
        if (!json.at("bridge_token").is_string() || !json.at("gui_token").is_string() ||
            !json.at("app_version").is_string()) {
            setError(error, "endpoint string field has the wrong type");
            return std::nullopt;
        }

        long long port = 0;
        long long pid = 0;
        long long catalog_version = 0;
        if (!parseInteger(json.at("port"), 1, 65535, port) ||
            !parseInteger(json.at("pid"), 1, std::numeric_limits<long long>::max(), pid) ||
            !parseInteger(json.at("catalog_version"), 1, std::numeric_limits<int>::max(),
                          catalog_version)) {
            setError(error, "endpoint integer field has the wrong type or range");
            return std::nullopt;
        }

        AgentEndpoint endpoint;
        endpoint.port = static_cast<int>(port);
        endpoint.bridge_token = json.at("bridge_token").get<std::string>();
        endpoint.gui_token = json.at("gui_token").get<std::string>();
        endpoint.pid = pid;
        endpoint.app_version = json.at("app_version").get<std::string>();
        endpoint.catalog_version = static_cast<int>(catalog_version);
        if (!isValidEndpoint(endpoint, error)) {
            return std::nullopt;
        }
        return endpoint;
    } catch (const std::exception &) {
        setError(error, "endpoint JSON is malformed");
        return std::nullopt;
    }
}

#ifdef _WIN32

std::string windowsError(const char *operation, DWORD code) {
    return std::string(operation) + " failed (Windows error " + std::to_string(code) + ")";
}

bool getCurrentUserSid(std::vector<std::byte> &token_data, PSID &sid) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    DWORD required_size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required_size);
    token_data.resize(required_size);
    const bool success =
        required_size != 0 && GetTokenInformation(token, TokenUser, token_data.data(),
                                                  required_size, &required_size) != FALSE;
    CloseHandle(token);
    if (!success) {
        token_data.clear();
        return false;
    }
    sid = reinterpret_cast<TOKEN_USER *>(token_data.data())->User.Sid;
    return IsValidSid(sid) != FALSE;
}

bool makePrivateSecurityAttributes(bool directory, SECURITY_ATTRIBUTES &attributes,
                                   PSECURITY_DESCRIPTOR &descriptor, std::string *error) {
    std::vector<std::byte> token_data;
    PSID sid = nullptr;
    if (!getCurrentUserSid(token_data, sid)) {
        setError(error, windowsError("get current user SID", GetLastError()));
        return false;
    }

    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(sid, &sid_text)) {
        setError(error, windowsError("convert current user SID", GetLastError()));
        return false;
    }
    std::wstring sddl = L"O:";
    sddl += sid_text;
    sddl += L"D:P(A;";
    if (directory) {
        sddl += L"OICI";
    }
    sddl += L";FA;;;";
    sddl += sid_text;
    sddl += L")";
    LocalFree(sid_text);

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr)) {
        setError(error, windowsError("create protected endpoint DACL", GetLastError()));
        return false;
    }
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    return true;
}

bool hasPrivateWindowsSecurity(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION file_info{};
    if (!GetFileInformationByHandle(handle, &file_info) || GetFileType(handle) != FILE_TYPE_DISK ||
        (file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (((file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory)) {
        return false;
    }

    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD security_result = GetSecurityInfo(
        handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
        nullptr, &dacl, nullptr, &descriptor);
    if (security_result != ERROR_SUCCESS) {
        return false;
    }

    std::vector<std::byte> token_data;
    PSID current_user = nullptr;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION acl_info{};
    bool valid =
        getCurrentUserSid(token_data, current_user) &&
        GetSecurityDescriptorControl(descriptor, &control, &revision) != FALSE &&
        (control & SE_DACL_PROTECTED) != 0 && owner != nullptr &&
        EqualSid(owner, current_user) != FALSE && dacl != nullptr &&
        GetAclInformation(dacl, &acl_info, sizeof(acl_info), AclSizeInformation) != FALSE &&
        acl_info.AceCount == 1;
    if (valid) {
        void *ace = nullptr;
        valid = GetAce(dacl, 0, &ace) != FALSE;
        if (valid) {
            const auto *header = static_cast<const ACE_HEADER *>(ace);
            valid =
                header->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                header->AceFlags == (directory ? (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : 0);
            if (valid) {
                const auto *allow = static_cast<const ACCESS_ALLOWED_ACE *>(ace);
                valid = allow->Mask == FILE_ALL_ACCESS &&
                        EqualSid(const_cast<DWORD *>(&allow->SidStart), current_user) != FALSE;
            }
        }
    }
    LocalFree(descriptor);
    return valid;
}

HANDLE openWindowsObject(const std::filesystem::path &path, DWORD access, DWORD share,
                         DWORD flags) {
    return CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
                       flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

bool readWindowsContents(HANDLE handle, std::string &contents, std::string *error) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > kMaxEndpointBytes) {
        setError(error, "endpoint file size is invalid");
        return false;
    }

    contents.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const DWORD amount = static_cast<DWORD>(
            std::min<std::size_t>(contents.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD bytes_read = 0;
        if (!ReadFile(handle, contents.data() + offset, amount, &bytes_read, nullptr) ||
            bytes_read == 0) {
            setError(error, windowsError("read endpoint file", GetLastError()));
            return false;
        }
        offset += bytes_read;
    }
    return true;
}

class WindowsTempFile {
  public:
    explicit WindowsTempFile(std::filesystem::path path) : m_path(std::move(path)) {}
    ~WindowsTempFile() {
        if (m_active) {
            DeleteFileW(m_path.c_str());
        }
    }
    void release() { m_active = false; }

  private:
    std::filesystem::path m_path;
    bool m_active = true;
};

std::filesystem::path makeWindowsTempPath(const std::filesystem::path &file,
                                          unsigned long long counter) {
    auto temp = file;
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(counter);
    return temp;
}

#else

std::string posixError(const char *operation, int code) {
    return std::string(operation) +
           " failed: " + std::error_code(code, std::generic_category()).message();
}

bool isPrivateDirectoryStatus(const struct stat &status) {
    return S_ISDIR(status.st_mode) && status.st_uid == geteuid() && (status.st_mode & 077) == 0 &&
           (status.st_mode & 0700) == 0700 && (status.st_mode & 07000) == 0;
}

class PosixEndpointDirectoryLock {
  public:
    explicit PosixEndpointDirectoryLock(const std::filesystem::path &parent, std::string *error) {
        m_fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (m_fd < 0) {
            setError(error, posixError("open endpoint directory", errno));
            return;
        }

        struct stat status {};
        if (fstat(m_fd, &status) != 0) {
            const int code = errno;
            close(m_fd);
            m_fd = -1;
            setError(error, posixError("inspect endpoint directory", code));
            return;
        }
        if (!isPrivateDirectoryStatus(status)) {
            close(m_fd);
            m_fd = -1;
            setError(error, "endpoint directory is not owned by the current user with mode 0700");
            return;
        }

        while (flock(m_fd, LOCK_EX) != 0) {
            if (errno == EINTR)
                continue;
            const int code = errno;
            close(m_fd);
            m_fd = -1;
            setError(error, posixError("lock endpoint directory", code));
            return;
        }
    }

    ~PosixEndpointDirectoryLock() {
        if (m_fd >= 0) {
            (void)flock(m_fd, LOCK_UN);
            (void)close(m_fd);
        }
    }

    explicit operator bool() const { return m_fd >= 0; }
    int fd() const { return m_fd; }

  private:
    int m_fd = -1;
};

bool isSafeEndpointStatus(const struct stat &status) {
    return S_ISREG(status.st_mode) && status.st_uid == geteuid() && (status.st_mode & 077) == 0 &&
           (status.st_mode & 07000) == 0 && (status.st_mode & S_IRUSR) != 0;
}

bool readPosixContents(int fd, const struct stat &status, std::string &contents,
                       std::string *error) {
    if (!isSafeEndpointStatus(status)) {
        setError(error, "endpoint file is not a current-user-owned private regular file");
        return false;
    }
    if (status.st_size < 0 || static_cast<unsigned long long>(status.st_size) > kMaxEndpointBytes) {
        setError(error, "endpoint file size is invalid");
        return false;
    }

    contents.clear();
    contents.reserve(static_cast<std::size_t>(status.st_size));
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = read(fd, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            setError(error, posixError("read endpoint file", errno));
            return false;
        }
        if (count == 0) {
            break;
        }
        if (static_cast<std::size_t>(count) > kMaxEndpointBytes - contents.size()) {
            setError(error, "endpoint file size is invalid");
            return false;
        }
        contents.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return true;
}

int openPosixEndpoint(const std::filesystem::path &file) {
    return open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
}

class PosixTempFile {
  public:
    PosixTempFile(int directory_fd, std::filesystem::path name)
        : m_directory_fd(directory_fd), m_name(std::move(name)) {}
    ~PosixTempFile() {
        if (m_active) {
            unlinkat(m_directory_fd, m_name.c_str(), 0);
        }
    }
    void release() { m_active = false; }

  private:
    int m_directory_fd;
    std::filesystem::path m_name;
    bool m_active = true;
};

std::filesystem::path makePosixTempPath(const std::filesystem::path &file,
                                        unsigned long long counter) {
    auto temp = file;
    temp +=
        ".tmp-" + std::to_string(static_cast<long long>(getpid())) + "-" + std::to_string(counter);
    return temp;
}

bool writeAll(int fd, std::string_view contents, std::string *error) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = write(fd, contents.data() + offset, contents.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            setError(error, posixError("write endpoint temp file", errno));
            return false;
        }
        if (written == 0) {
            setError(error, "write endpoint temp file made no progress");
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool readPosixEndpointAt(int directory_fd, const char *name, std::string &contents,
                         struct stat &file_status, std::string *error) {
    const int file_fd = openat(directory_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (file_fd < 0) {
        setError(error, posixError("open endpoint file", errno));
        return false;
    }
    const bool stat_ok = fstat(file_fd, &file_status) == 0;
    const int stat_error = errno;
    const bool read_ok = stat_ok && readPosixContents(file_fd, file_status, contents, error);
    const int close_result = close(file_fd);
    if (!stat_ok) {
        setError(error, posixError("inspect endpoint file", stat_error));
        return false;
    }
    if (!read_ok) {
        return false;
    }
    if (close_result != 0) {
        setError(error, posixError("close endpoint file", errno));
        return false;
    }
    return true;
}

#endif

} // namespace

std::string fnv1a64Hex(std::string_view bytes) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    constexpr char kHexDigits[] = "0123456789abcdef";

    std::uint64_t hash = kOffsetBasis;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= kPrime;
    }

    std::string result(16, '0');
    for (std::size_t index = result.size(); index > 0; --index) {
        result[index - 1] = kHexDigits[hash & 0x0f];
        hash >>= 4;
    }
    return result;
}

std::filesystem::path currentExecutableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(256);
    while (buffer.size() <= 32768) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "GetModuleFileNameW");
        }
        if (length < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(std::min<std::size_t>(buffer.size() * 2, 32769));
    }
    throw std::runtime_error("executable path exceeds the Windows path limit");
#else
    std::vector<char> buffer(256);
    while (buffer.size() <= 1024 * 1024) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            throw std::system_error(errno, std::generic_category(), "readlink /proc/self/exe");
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            return std::filesystem::path(
                       std::string(buffer.data(), static_cast<std::size_t>(length)))
                .parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
    throw std::runtime_error("executable path exceeds the supported length");
#endif
}

std::string agentInstallKey(const std::filesystem::path &exe_dir) {
    const auto canonical = std::filesystem::weakly_canonical(exe_dir);
#ifdef _WIN32
    auto native_path = canonical.native();
    if (native_path.size() > std::numeric_limits<DWORD>::max() ||
        CharLowerBuffW(native_path.data(), static_cast<DWORD>(native_path.size())) !=
            native_path.size()) {
        throw std::runtime_error("failed to lowercase canonical executable directory");
    }
    if (native_path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("canonical executable directory is too long");
    }
    const int utf8_size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native_path.data(),
                            static_cast<int>(native_path.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        throw std::runtime_error("failed to encode executable directory as UTF-8");
    }
    std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native_path.data(),
                            static_cast<int>(native_path.size()), utf8.data(), utf8_size, nullptr,
                            nullptr) != utf8_size) {
        throw std::runtime_error("failed to encode executable directory as UTF-8");
    }
    return fnv1a64Hex(utf8);
#else
    return fnv1a64Hex(canonical.native());
#endif
}

std::optional<std::filesystem::path> defaultAgentEndpointDirectory(std::string *error) {
#ifdef _WIN32
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        setError(error, windowsError("read LOCALAPPDATA", GetLastError()));
        return std::nullopt;
    }
    std::wstring local_app_data(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), required);
    if (length == 0 || length >= required) {
        setError(error, windowsError("read LOCALAPPDATA", GetLastError()));
        return std::nullopt;
    }
    local_app_data.resize(length);
    setError(error, {});
    return std::filesystem::path(local_app_data) / L"rf-sim" / L"agent";
#else
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        setError(error, {});
        return std::filesystem::path(runtime_dir) / "rf-sim";
    }
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        setError(error, "HOME is not set and XDG_RUNTIME_DIR is unavailable");
        return std::nullopt;
    }
    setError(error, {});
    return std::filesystem::path(home) / ".rf-sim" / "run";
#endif
}

std::filesystem::path agentEndpointFileName(const std::string &install_key) {
    return std::filesystem::path("agent-endpoint-" + install_key + ".json");
}

bool ensurePrivateDirectory(const std::filesystem::path &dir, std::string *error) {
    if (dir.empty()) {
        setError(error, "endpoint directory path is empty");
        return false;
    }
#ifdef _WIN32
    std::error_code filesystem_error;
    const auto parent = dir.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            setError(error,
                     "create endpoint directory parent failed: " + filesystem_error.message());
            return false;
        }
    }

    SECURITY_ATTRIBUTES attributes{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!makePrivateSecurityAttributes(true, attributes, descriptor, error)) {
        return false;
    }
    const BOOL created = CreateDirectoryW(dir.c_str(), &attributes);
    const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!created && create_error != ERROR_ALREADY_EXISTS) {
        setError(error, windowsError("create endpoint directory", create_error));
        return false;
    }

    HANDLE handle =
        openWindowsObject(dir, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_FLAG_BACKUP_SEMANTICS);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(error, windowsError("open endpoint directory", GetLastError()));
        return false;
    }
    const bool safe = hasPrivateWindowsSecurity(handle, true);
    const DWORD close_error = CloseHandle(handle) ? ERROR_SUCCESS : GetLastError();
    if (!safe) {
        setError(error, "endpoint directory is not protected for the current user");
        return false;
    }
    if (close_error != ERROR_SUCCESS) {
        setError(error, windowsError("close endpoint directory", close_error));
        return false;
    }
#else
    std::error_code filesystem_error;
    const auto parent = dir.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            setError(error,
                     "create endpoint directory parent failed: " + filesystem_error.message());
            return false;
        }
    }

    const bool created = mkdir(dir.c_str(), 0700) == 0;
    const int create_error = created ? 0 : errno;
    if (!created && create_error != EEXIST) {
        setError(error, posixError("create endpoint directory", create_error));
        return false;
    }
    if (created && chmod(dir.c_str(), 0700) != 0) {
        setError(error, posixError("set endpoint directory permissions", errno));
        return false;
    }

    struct stat status {};
    if (lstat(dir.c_str(), &status) != 0) {
        setError(error, posixError("inspect endpoint directory", errno));
        return false;
    }
    if (!isPrivateDirectoryStatus(status)) {
        setError(error, "endpoint directory is not owned by the current user with mode 0700");
        return false;
    }
#endif
    setError(error, {});
    return true;
}

bool writeAgentEndpoint(const std::filesystem::path &file, const AgentEndpoint &endpoint,
                        std::string *error) {
    if (file.empty() || file.filename().empty()) {
        setError(error, "endpoint file path is empty");
        return false;
    }
    if (!isValidEndpoint(endpoint, error)) {
        return false;
    }
    const auto parent = file.parent_path();
    if (parent.empty()) {
        setError(error, "endpoint file must have a private parent directory");
        return false;
    }
    if (!ensurePrivateDirectory(parent, error)) {
        return false;
    }

    std::string serialized;
    try {
        serialized = endpointJson(endpoint).dump();
    } catch (const std::exception &) {
        setError(error, "endpoint JSON could not be serialized");
        return false;
    }

#ifdef _WIN32
    static std::atomic<unsigned long long> next_temp{0};
    SECURITY_ATTRIBUTES attributes{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!makePrivateSecurityAttributes(false, attributes, descriptor, error)) {
        return false;
    }

    std::filesystem::path temp_path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD create_error = ERROR_SUCCESS;
    for (unsigned int attempt = 0; attempt < 128; ++attempt) {
        temp_path = makeWindowsTempPath(file, next_temp.fetch_add(1));
        handle = CreateFileW(temp_path.c_str(), GENERIC_WRITE | READ_CONTROL | DELETE, 0,
                             &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        create_error = GetLastError();
        if (create_error != ERROR_FILE_EXISTS && create_error != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    LocalFree(descriptor);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(error, windowsError("create endpoint temp file", create_error));
        return false;
    }
    WindowsTempFile temp_guard(temp_path);
    if (!hasPrivateWindowsSecurity(handle, false)) {
        setError(error, "endpoint temp file did not receive the protected current-user DACL");
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
        CloseHandle(handle);
        return false;
    }

    std::size_t offset = 0;
    bool write_ok = true;
    while (offset < serialized.size()) {
        const DWORD amount = static_cast<DWORD>(
            std::min<std::size_t>(serialized.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD bytes_written = 0;
        if (!WriteFile(handle, serialized.data() + offset, amount, &bytes_written, nullptr) ||
            bytes_written == 0) {
            setError(error, windowsError("write endpoint temp file", GetLastError()));
            write_ok = false;
            break;
        }
        offset += bytes_written;
    }
    if (write_ok && !FlushFileBuffers(handle)) {
        setError(error, windowsError("flush endpoint temp file", GetLastError()));
        write_ok = false;
    }
    if (!write_ok) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
    }
    const BOOL close_ok = CloseHandle(handle);
    if (!close_ok) {
        if (write_ok) {
            setError(error, windowsError("close endpoint temp file", GetLastError()));
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
        write_ok = false;
    }
    if (!write_ok) {
        return false;
    }
    if (!MoveFileExW(temp_path.c_str(), file.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        setError(error, windowsError("replace endpoint file", GetLastError()));
        return false;
    }
    temp_guard.release();
#else
    PosixEndpointDirectoryLock directory(parent, error);
    if (!directory) {
        return false;
    }

    static std::atomic<unsigned long long> next_temp{0};
    std::filesystem::path temp_name;
    int fd = -1;
    int create_error = 0;
    for (unsigned int attempt = 0; attempt < 128; ++attempt) {
        temp_name = makePosixTempPath(file, next_temp.fetch_add(1)).filename();
        fd = openat(directory.fd(), temp_name.c_str(),
                    O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) {
            break;
        }
        create_error = errno;
        if (create_error != EEXIST) {
            break;
        }
    }
    if (fd < 0) {
        setError(error, posixError("create endpoint temp file", create_error));
        return false;
    }
    PosixTempFile temp_guard(directory.fd(), temp_name);
    if (fchmod(fd, 0600) != 0) {
        setError(error, posixError("set endpoint temp file permissions", errno));
        close(fd);
        return false;
    }

    bool write_ok = writeAll(fd, serialized, error);
    if (write_ok && fsync(fd) != 0) {
        setError(error, posixError("flush endpoint temp file", errno));
        write_ok = false;
    }
    const int close_result = close(fd);
    if (close_result != 0 && write_ok) {
        setError(error, posixError("close endpoint temp file", errno));
        write_ok = false;
    }
    if (!write_ok) {
        return false;
    }
    if (renameat(directory.fd(), temp_name.c_str(), directory.fd(), file.filename().c_str()) != 0) {
        setError(error, posixError("replace endpoint file", errno));
        return false;
    }
    temp_guard.release();
#endif
    setError(error, {});
    return true;
}

std::optional<AgentEndpoint> readAgentEndpoint(const std::filesystem::path &file,
                                               std::string *error) {
    if (file.empty()) {
        setError(error, "endpoint file path is empty");
        return std::nullopt;
    }
#ifdef _WIN32
    HANDLE handle = openWindowsObject(file, GENERIC_READ | READ_CONTROL,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_ATTRIBUTE_NORMAL);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(error, windowsError("open endpoint file", GetLastError()));
        return std::nullopt;
    }
    std::string contents;
    const bool safe = hasPrivateWindowsSecurity(handle, false);
    const bool read_ok = safe && readWindowsContents(handle, contents, error);
    const DWORD close_error = CloseHandle(handle) ? ERROR_SUCCESS : GetLastError();
    if (!safe) {
        setError(error, "endpoint file is not protected for the current user");
        return std::nullopt;
    }
    if (!read_ok) {
        return std::nullopt;
    }
    if (close_error != ERROR_SUCCESS) {
        setError(error, windowsError("close endpoint file", close_error));
        return std::nullopt;
    }
#else
    const int fd = openPosixEndpoint(file);
    if (fd < 0) {
        setError(error, posixError("open endpoint file", errno));
        return std::nullopt;
    }
    struct stat status {};
    const bool stat_ok = fstat(fd, &status) == 0;
    const int stat_error = errno;
    std::string contents;
    const bool read_ok = stat_ok && readPosixContents(fd, status, contents, error);
    const int close_result = close(fd);
    if (!stat_ok) {
        setError(error, posixError("inspect endpoint file", stat_error));
        return std::nullopt;
    }
    if (!read_ok) {
        return std::nullopt;
    }
    if (close_result != 0) {
        setError(error, posixError("close endpoint file", errno));
        return std::nullopt;
    }
#endif
    auto endpoint = parseEndpoint(contents, error);
    if (endpoint) {
        setError(error, {});
    }
    return endpoint;
}

bool removeAgentEndpointIfOwned(const std::filesystem::path &file, std::string_view gui_token) {
    if (file.empty() || file.filename().empty()) {
        return false;
    }
#ifdef _WIN32
    HANDLE handle = openWindowsObject(file, DELETE | GENERIC_READ | READ_CONTROL,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_ATTRIBUTE_NORMAL);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool removed = false;
    if (hasPrivateWindowsSecurity(handle, false)) {
        std::string contents;
        std::string ignored_error;
        if (readWindowsContents(handle, contents, &ignored_error)) {
            const auto endpoint = parseEndpoint(contents, &ignored_error);
            if (endpoint && agentTokensEqual(endpoint->gui_token, gui_token)) {
                FILE_DISPOSITION_INFO disposition{TRUE};
                removed = SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                                     sizeof(disposition)) != FALSE;
            }
        }
    }
    const BOOL close_ok = CloseHandle(handle);
    return removed && close_ok != FALSE;
#else
    const auto parent =
        file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
    const auto name = file.filename();
    PosixEndpointDirectoryLock directory(parent, nullptr);
    if (!directory) {
        return false;
    }
    const int directory_fd = directory.fd();

    std::string contents;
    struct stat file_status {};
    std::string ignored_error;
    if (!readPosixEndpointAt(directory_fd, name.c_str(), contents, file_status, &ignored_error)) {
        return false;
    }
    const auto endpoint = parseEndpoint(contents, &ignored_error);
    if (!endpoint || !agentTokensEqual(endpoint->gui_token, gui_token)) {
        return false;
    }

    struct stat current_status {};
    const bool same_file =
        fstatat(directory_fd, name.c_str(), &current_status, AT_SYMLINK_NOFOLLOW) == 0 &&
        current_status.st_dev == file_status.st_dev &&
        current_status.st_ino == file_status.st_ino && isSafeEndpointStatus(current_status);
    return same_file && unlinkat(directory_fd, name.c_str(), 0) == 0;
#endif
}
