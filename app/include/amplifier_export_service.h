#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct AmplifierExportRequest {
    std::filesystem::path project_root;
    std::string part_number;
    std::string manufacturer;
    std::vector<std::pair<double, double>> gain_db_vs_freq;
    std::vector<std::pair<double, double>> nf_db_vs_freq;
    double input_return_loss_db = 20.0;
    double output_return_loss_db = 20.0;
    std::string description;
    std::string notes;
};

struct AmplifierExportResult {
    bool ok = false;
    std::filesystem::path json_path;
    std::filesystem::path s2p_path;
    std::string message;
};

AmplifierExportResult exportAmplifier(const AmplifierExportRequest &request);
