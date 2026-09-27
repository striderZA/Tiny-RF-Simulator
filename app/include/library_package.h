#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace library_package {

inline constexpr int kFormatVersion = 1;
inline constexpr const char *kFormatId = "rf-sim-library-package";
inline constexpr std::size_t kMaxArchiveMembers = 4096;
inline constexpr std::uint64_t kMaxExpandedBytes = 1024ull * 1024 * 1024; // 1 GiB total

} // namespace library_package

struct LibraryPackageExportResult {
    bool ok = false;
    std::string error;       // package-level failure; empty when ok
    std::string output_path; // written archive when ok
    int definitions = 0;
    int assets = 0;
};

struct LibraryPackageImportResult {
    bool ok = false;                    // false = whole-package refusal, nothing written
    std::string error;                  // package-level failure; empty when ok
    int imported = 0;                   // component definitions installed
    std::vector<std::string> conflicts; // one line per skipped identity conflict
    std::string installed_dir;          // <root>/<package_name>; empty when imported == 0
};

// Writes every loader-valid component JSON under `source_root` plus only the
// files it references (data_files, sparam_filepath, sparam_path) into a ZIP at
// `output_path`. A failure removes any partial output.
LibraryPackageExportResult exportLibraryPackage(const std::filesystem::path &source_root,
                                                const std::filesystem::path &output_path);
