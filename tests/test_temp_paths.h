#pragma once

#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// test_temp_paths — cross-process uniqueness token for test scratch files.
//
// `catch_discover_tests(tests)` registers one CTest entry per TEST_CASE, so
// under `ctest -jN` several TEST_CASEs of the same file run as separate
// processes at the same time. A `static` counter is therefore NOT a
// uniqueness token across those processes: it restarts at 0 in every one of
// them, and two concurrent cases then open the same scratch file and clobber
// each other's save/load state. A process id is unique among live processes,
// so `processTag()` plus a caller's own counter is unique for every
// concurrently running case without any coordination or randomness.
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

} // namespace test_temp_paths
