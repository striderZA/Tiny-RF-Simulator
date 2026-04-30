#include "touchstone_parser.h"
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>

std::string TouchstoneParser::stripComment(const std::string& line) {
    size_t pos = line.find('!');
    if (pos == std::string::npos) return line;
    return line.substr(0, pos);
}

bool TouchstoneParser::parseOptionLine(const std::string& line, TouchstoneData& data) {
    std::istringstream iss(line);
    std::string token;
    iss >> token;
    if (token != "#") return false;

    while (iss >> token) {
        // Case-insensitive compare
        auto lower = [](const std::string& s) {
            std::string r = s;
            for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return r;
        };
        std::string t = lower(token);

        if (t == "hz") data.freq_unit = TouchstoneData::FrequencyUnit::Hz;
        else if (t == "khz") data.freq_unit = TouchstoneData::FrequencyUnit::kHz;
        else if (t == "mhz") data.freq_unit = TouchstoneData::FrequencyUnit::MHz;
        else if (t == "ghz") data.freq_unit = TouchstoneData::FrequencyUnit::GHz;
        else if (t == "s") data.parameter = TouchstoneData::Parameter::S;
        else if (t == "y") data.parameter = TouchstoneData::Parameter::Y;
        else if (t == "z") data.parameter = TouchstoneData::Parameter::Z;
        else if (t == "h") data.parameter = TouchstoneData::Parameter::H;
        else if (t == "g") data.parameter = TouchstoneData::Parameter::G;
        else if (t == "db") data.format = TouchstoneData::Format::DB;
        else if (t == "ma") data.format = TouchstoneData::Format::MA;
        else if (t == "ri") data.format = TouchstoneData::Format::RI;
        else if (t == "r") {
            double r;
            if (iss >> r) data.reference_impedance = r;
        }
    }
    return true;
}

std::complex<double> TouchstoneParser::parsePair(double a, double b, TouchstoneData::Format fmt) {
    switch (fmt) {
        case TouchstoneData::Format::DB: {
            double mag = std::pow(10.0, a / 20.0);
            double rad = b * std::numbers::pi / 180.0;
            return std::complex<double>(mag * std::cos(rad), mag * std::sin(rad));
        }
        case TouchstoneData::Format::MA: {
            double rad = b * std::numbers::pi / 180.0;
            return std::complex<double>(a * std::cos(rad), a * std::sin(rad));
        }
        case TouchstoneData::Format::RI:
            return std::complex<double>(a, b);
    }
    return std::complex<double>(a, b);
}

std::optional<TouchstoneData> TouchstoneParser::parse(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return std::nullopt;

    TouchstoneData data;
    bool option_line_found = false;
    bool has_version_keyword = false;
    std::string line;
    std::vector<double> raw_values; // all numeric values from data lines

    while (std::getline(file, line)) {
        // Normalize line endings (CR, LF, CRLF)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string stripped = stripComment(line);
        // Trim whitespace
        size_t start = stripped.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = stripped.find_last_not_of(" \t\r\n");
        stripped = stripped.substr(start, end - start + 1);

        if (stripped.empty()) continue;

        // Detect version 2.0 keyword
        if (stripped.size() > 10 && stripped[0] == '[') {
            std::string lower = stripped;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find("[version] 2.0") != std::string::npos) {
                has_version_keyword = true;
            }
            // For now, skip all v2.0 keywords; we'll handle them later
            if (has_version_keyword) continue;
        }

        if (stripped[0] == '#') {
            if (!parseOptionLine(stripped, data)) return std::nullopt;
            option_line_found = true;
            continue;
        }

        // Data line: read all numbers
        std::istringstream iss(stripped);
        double val;
        while (iss >> val) {
            raw_values.push_back(val);
        }
    }

    if (!option_line_found) return std::nullopt;

    // Infer port count from file extension if not explicitly known
    // .s1p -> 1 port, .s2p -> 2 ports, etc.
    size_t dot = filepath.rfind('.');
    if (dot != std::string::npos && dot + 2 < filepath.size() && filepath[dot + 1] == 's') {
        char port_char = filepath[dot + 2];
        if (port_char >= '1' && port_char <= '9') {
            data.num_ports = port_char - '0';
        }
    }
    if (data.num_ports == 0) {
        // Try to infer from data density
        // v1.0 2-port: 9 values per point (freq + 4 pairs)
        // v1.0 1-port: 3 values per point (freq + 1 pair)
        // We can't reliably infer without hints, so default to 2 for .s2p files
        data.num_ports = 2;
    }

    int pairs_per_freq = 0;
    if (data.num_ports == 1) {
        pairs_per_freq = 1;
    } else if (data.num_ports == 2) {
        pairs_per_freq = 4; // N11, N21, N12, N22
    } else {
        pairs_per_freq = data.num_ports * data.num_ports;
    }
    int values_per_freq = 1 + pairs_per_freq * 2;

    if (raw_values.size() % values_per_freq != 0) {
        // Mismatch: try to be lenient or return null
        return std::nullopt;
    }

    size_t num_freq_points = raw_values.size() / values_per_freq;
    data.frequencies.reserve(num_freq_points);
    data.parameters.reserve(num_freq_points);

    for (size_t i = 0; i < num_freq_points; ++i) {
        size_t base = i * values_per_freq;
        double freq = raw_values[base];
        data.frequencies.push_back(data.freqToHz(freq));

        std::vector<std::complex<double>> params;
        params.reserve(pairs_per_freq);
        for (int p = 0; p < pairs_per_freq; ++p) {
            double a = raw_values[base + 1 + p * 2];
            double b = raw_values[base + 1 + p * 2 + 1];
            params.push_back(parsePair(a, b, data.format));
        }
        data.parameters.push_back(std::move(params));
    }

    return data;
}

double TouchstoneData::freqToHz(double freq) const {
    switch (freq_unit) {
        case FrequencyUnit::Hz: return freq;
        case FrequencyUnit::kHz: return freq * 1e3;
        case FrequencyUnit::MHz: return freq * 1e6;
        case FrequencyUnit::GHz: return freq * 1e9;
    }
    return freq;
}
