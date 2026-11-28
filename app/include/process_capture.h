#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simulator::process_capture {

struct Result {
    // A missing program is reported by run() returning nullopt; after a lookup
    // succeeds, started is false only when the process could not be launched.
    bool started = false;
    bool timed_out = false;
    bool output_truncated = false;
    int exit_code = -1;
    std::string output;
};

struct Options {
    std::filesystem::path working_directory;
    std::chrono::milliseconds timeout{1000};
    std::size_t max_output_bytes = 4 * 1024 * 1024;
    // Exact environment variable names to drop from the child's environment.
    std::vector<std::string> remove_environment_names;
};

// Resolves a bare program name through PATH (adding `.exe` on Windows).
// Relative PATH entries are ignored, so a project-local file is never run.
std::optional<std::filesystem::path> findExecutable(std::string_view program);

// Runs `program` without a shell, capturing standard output. Returns nullopt
// when the program is absent from PATH. A child that exceeds the deadline,
// writes more than max_output_bytes, or exits non-zero is reported in the
// Result; the caller decides which of those is a failure.
std::optional<Result> run(std::string_view program, const std::vector<std::string> &arguments,
                          const Options &options = {});

} // namespace simulator::process_capture
