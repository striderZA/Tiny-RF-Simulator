#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

// test_temp_paths - cross-process isolation for test scratch files.
//
// `catch_discover_tests(tests)` registers one CTest entry per TEST_CASE, so
// under `ctest -jN` several TEST_CASEs run as separate processes at the same
// time. Fixed names in the shared temp directory then collide: concurrent
// cases open the same scratch file and clobber each other's state. The app's
// external-tool run root (`<temp>/rf-sim-extension-run`) is one such name, and
// it is hard-coded in the app, so renaming the tests' own paths cannot cover it.
//
// Each test process therefore redirects its temp directory to a root of its
// own (ProcessTempRoot below). Every temp_directory_path() call, including the
// app's, and every fixed name built on it is then private to that process. The
// root is removed on normal exit. Include this header directly in each
// test translation unit that creates temporary files.
namespace test_temp_paths {

inline unsigned long processId() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// Filename-safe prefix for scratch files, e.g. "p12345".
inline std::string processTag() { return "p" + std::to_string(processId()); }

// Redirects this process's temp directory to `<real temp>/rfsim_test_<tag>`.
// The constructor runs during static initialization, before any test starts;
// the destructor removes that root on normal exit. If the root cannot be created, the
// temp directory is left unchanged and root() stays empty; the regression case
// in tests/test_issue238_temp_isolation.cpp fails on that state.
class ProcessTempRoot {
  public:
    ProcessTempRoot() {
        std::error_code ec;
        const std::filesystem::path real_temp = std::filesystem::temp_directory_path(ec);
        if (ec) {
            return;
        }
        const std::filesystem::path dir = real_temp / ("rfsim_test_" + processTag());
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return;
        }
        m_root = dir;
#ifdef _WIN32
        const std::wstring wide = dir.wstring();
        SetEnvironmentVariableW(L"TMP", wide.c_str());
        SetEnvironmentVariableW(L"TEMP", wide.c_str());
#else
        setenv("TMPDIR", dir.c_str(), 1);
#endif
    }
    ~ProcessTempRoot() {
        if (m_root.empty()) {
            return;
        }
        // Git writes object files read-only on Windows, and remove_all cannot delete
        // read-only files, so make every entry writable before removing the tree.
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(m_root, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code perm_ec;
            std::filesystem::permissions(it->path(), std::filesystem::perms::owner_write,
                                         std::filesystem::perm_options::add, perm_ec);
        }
        ec.clear();
        std::filesystem::remove_all(m_root, ec);
    }
    ProcessTempRoot(const ProcessTempRoot &) = delete;
    ProcessTempRoot &operator=(const ProcessTempRoot &) = delete;

    const std::filesystem::path &root() const { return m_root; }

  private:
    std::filesystem::path m_root;
};

inline const ProcessTempRoot kProcessTempRoot{};

} // namespace test_temp_paths
