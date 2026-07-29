#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class ExtensionKind {
    DataPack,
    ExternalTool,
};

enum class ExtensionCapability {
    Generator,
    Importer,
};

enum class ExtensionStatusKind {
    Ok,
    Invalid,
    Incompatible,
};

struct ExtensionMenuEntry {
    std::string location;
    std::string label;
};

struct ExtensionValidationIssue {
    std::string field;
    std::string message;
};

struct ExtensionManifest {
    int schema_version = 0;
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string min_app_version;
    ExtensionKind kind = ExtensionKind::DataPack;
    std::vector<ExtensionCapability> capabilities;
    std::filesystem::path manifest_path;
    std::filesystem::path root_dir;
    std::filesystem::path entry_path;
    std::vector<ExtensionMenuEntry> menus;
    std::vector<std::filesystem::path> library_roots;
    std::vector<std::filesystem::path> data_roots;
};

std::optional<ExtensionManifest> parseExtensionManifest(
    const std::filesystem::path &manifest_path,
    std::vector<ExtensionValidationIssue> &issues);
