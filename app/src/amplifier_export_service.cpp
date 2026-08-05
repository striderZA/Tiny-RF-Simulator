#include "amplifier_export_service.h"
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

double dbToLinearMag(double db) { return std::pow(10.0, db / 20.0); }

double averageGainDb(const std::vector<std::pair<double, double>> &points) {
    if (points.empty())
        return 0.0;
    double sum = 0.0;
    for (const auto &[freq_Hz, gain_dB] : points) {
        (void)freq_Hz;
        sum += gain_dB;
    }
    return sum / static_cast<double>(points.size());
}

double averageValue(const std::vector<std::pair<double, double>> &points) {
    if (points.empty())
        return 0.0;
    double sum = 0.0;
    for (const auto &[x, y] : points) {
        (void)x;
        sum += y;
    }
    return sum / static_cast<double>(points.size());
}

std::string sanitizePathSegment(const std::string &input, const std::string &fallback) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == ' ')
            out.push_back(static_cast<char>(c));
    }
    const auto start = out.find_first_not_of(' ');
    if (start == std::string::npos)
        return fallback;
    const auto end = out.find_last_not_of(' ');
    out = out.substr(start, end - start + 1);
    return out.empty() ? fallback : out;
}

std::string nextAvailableStem(const std::filesystem::path &directory,
                              const std::string &part_number) {
    std::string candidate = part_number;
    int n = 2;
    while (std::filesystem::exists(directory / (candidate + ".json")) ||
           std::filesystem::exists(directory / (candidate + ".s2p"))) {
        candidate = part_number + "-" + std::to_string(n++);
    }
    return candidate;
}

nlohmann::json buildDefinition(const AmplifierExportRequest &request, const std::string &s2p_name) {
    nlohmann::json j;
    j["schema_version"] = 2;
    j["type"] = "amplifier";
    j["part_number"] = request.part_number;
    j["manufacturer"] = request.manufacturer;
    if (!request.description.empty())
        j["description"] = request.description;

    j["parameters"] = nlohmann::json::object();
    if (!request.gain_db_vs_freq.empty())
        j["parameters"]["gain_dB"] = averageValue(request.gain_db_vs_freq);
    if (!request.nf_db_vs_freq.empty())
        j["parameters"]["nf_dB"] = averageValue(request.nf_db_vs_freq);
    j["parameters"]["nf_db_vs_freq"] = nlohmann::json::array();
    for (const auto &[freq_Hz, nf_dB] : request.nf_db_vs_freq)
        j["parameters"]["nf_db_vs_freq"].push_back({freq_Hz, nf_dB});

    j["data_files"] = nlohmann::json::array({{{"type", "s_parameters"}, {"path", s2p_name}}});
    if (!request.notes.empty())
        j["notes"] = request.notes;
    return j;
}

bool writeS2p(const std::filesystem::path &path, const AmplifierExportRequest &request) {
    if (request.gain_db_vs_freq.empty())
        return false;

    std::ofstream out(path);
    if (!out.is_open())
        return false;

    const double s11_mag = dbToLinearMag(-std::abs(request.input_return_loss_db));
    const double s22_mag = dbToLinearMag(-std::abs(request.output_return_loss_db));

    out << "! " << request.part_number << " - synthesized unilateral S-parameters\n";
    out << "# Hz S MA R 50\n";
    for (const auto &[freq_Hz, gain_dB] : request.gain_db_vs_freq) {
        out << freq_Hz << ' ' << s11_mag << " 0.0 " << dbToLinearMag(gain_dB) << " 0.0 0.0 0.0 "
            << s22_mag << " 0.0\n";
    }
    return true;
}

} // namespace

AmplifierExportResult exportAmplifier(const AmplifierExportRequest &request) {
    AmplifierExportResult result;

    const auto vendor_dir = request.project_root / "rf-sim-libraries" / "amplifiers" /
                            sanitizePathSegment(request.manufacturer, "Unknown Vendor");
    std::filesystem::create_directories(vendor_dir);
    const std::string stem =
        nextAvailableStem(vendor_dir, sanitizePathSegment(request.part_number, "AMPLIFIER"));

    result.json_path = vendor_dir / (stem + ".json");
    result.s2p_path = vendor_dir / (stem + ".s2p");

    if (!writeS2p(result.s2p_path, request)) {
        result.message = "could not write output s2p";
        return result;
    }

    const auto definition = buildDefinition(request, result.s2p_path.filename().string());
    std::ofstream json_out(result.json_path);
    if (!json_out.is_open()) {
        std::error_code ec;
        std::filesystem::remove(result.s2p_path, ec);
        result.message = "could not open output json";
        return result;
    }
    json_out << definition.dump(2) << '\n';
    json_out.close();
    result.ok = true;
    result.message = "ok";
    return result;
}
