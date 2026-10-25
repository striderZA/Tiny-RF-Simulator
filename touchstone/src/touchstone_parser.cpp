#include "touchstone_parser.h"
#include "logging_core.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string_view>
#include <system_error>

namespace {

// Parses one whitespace-delimited token as a complete, finite decimal number.
// Anything else (a lone sign, "1e", "0.1x", "nan") is rejected.
bool parseDataToken(std::string_view token, double &value) {
    if (token.size() > 1 && token.front() == '+') {
        token.remove_prefix(1);
        if (token.front() == '+' || token.front() == '-')
            return false;
    }
    const char *first = token.data();
    const char *last = first + token.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    return ec == std::errc() && ptr == last && std::isfinite(value);
}

// Appends every numeric token in s to out.
void appendNumbers(const std::string &s, std::vector<double> &out) {
    std::istringstream iss(s);
    double v;
    while (iss >> v)
        out.push_back(v);
}

} // namespace

std::string TouchstoneParser::stripComment(const std::string &line) {
    size_t pos = line.find('!');
    if (pos == std::string::npos)
        return line;
    return line.substr(0, pos);
}

bool TouchstoneParser::parseOptionLine(const std::string &line, TouchstoneData &data) {
    std::istringstream iss(line);
    std::string token;
    iss >> token;
    if (token != "#")
        return false;

    while (iss >> token) {
        // Case-insensitive compare
        auto lower = [](const std::string &s) {
            std::string r = s;
            for (char &c : r)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return r;
        };
        std::string t = lower(token);

        if (t == "hz")
            data.freq_unit = TouchstoneData::FrequencyUnit::Hz;
        else if (t == "khz")
            data.freq_unit = TouchstoneData::FrequencyUnit::kHz;
        else if (t == "mhz")
            data.freq_unit = TouchstoneData::FrequencyUnit::MHz;
        else if (t == "ghz")
            data.freq_unit = TouchstoneData::FrequencyUnit::GHz;
        else if (t == "s")
            data.parameter = TouchstoneData::Parameter::S;
        else if (t == "y")
            data.parameter = TouchstoneData::Parameter::Y;
        else if (t == "z")
            data.parameter = TouchstoneData::Parameter::Z;
        else if (t == "h")
            data.parameter = TouchstoneData::Parameter::H;
        else if (t == "g")
            data.parameter = TouchstoneData::Parameter::G;
        else if (t == "db")
            data.format = TouchstoneData::Format::DB;
        else if (t == "ma")
            data.format = TouchstoneData::Format::MA;
        else if (t == "ri")
            data.format = TouchstoneData::Format::RI;
        else if (t == "r") {
            double r;
            if (iss >> r)
                data.reference_impedance = r;
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

std::optional<TouchstoneData> TouchstoneParser::parse(const std::string &filepath) {
    // R2: Cap the file size before reading anything. A legitimate Touchstone
    // dataset at the frequency-point cap below (10M points x 9 values/point x
    // ~10 bytes/value) stays far under 256 MiB, so anything larger cannot
    // parse successfully and would otherwise be buffered whole into
    // raw_values (up to ~960 MB at the point cap). Binary mode keeps tellg()
    // exact; the read loop below already strips '\r' from line endings, so
    // CRLF files parse identically to text mode.
    constexpr std::streamoff MAX_FILE_BYTES = 256LL * 1024 * 1024; // 256 MiB
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;

    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    if (file_size < 0 || file_size > MAX_FILE_BYTES)
        return std::nullopt;
    file.seekg(0, std::ios::beg);

    TouchstoneData data;
    bool option_line_found = false;
    bool has_version_keyword = false;
    bool has_two_port_data_order = false;
    bool two_port_data_order_is_12_21 = false;
    std::string line;
    std::vector<double> raw_values; // all numeric values from data lines
    bool in_reference = false;
    std::vector<double> reference_values; // v2.0 [Reference] values, in file order

    // Infer port count from file extension if not explicitly known
    // .s1p -> 1 port, .s2p -> 2 ports, etc. (Hoisted ahead of the read loop
    // so the in-loop frequency-point cap below knows the values per point.)
    size_t dot = filepath.rfind('.');
    if (dot != std::string::npos && dot + 1 < filepath.size() &&
        (filepath[dot + 1] == 's' || filepath[dot + 1] == 'S')) {
        // Exact .sNp / .SNP extension: N is 1-4 digits (ports 1-9999 keeps the
        // int port math below overflow-free) and the extension ends after 'p'.
        const size_t digits_begin = dot + 2;
        size_t pos = digits_begin;
        int ports = 0;
        while (pos < filepath.size() && pos - digits_begin < 4 && filepath[pos] >= '0' &&
               filepath[pos] <= '9') {
            ports = ports * 10 + (filepath[pos] - '0');
            ++pos;
        }
        const bool p_terminates =
            pos + 1 == filepath.size() && (filepath[pos] == 'p' || filepath[pos] == 'P');
        if (pos > digits_begin && p_terminates && ports >= 1)
            data.num_ports = ports;
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

    // R2: Upper bound on frequency points to prevent OOM. Enforced DURING the
    // read loop: once more than MAX_FREQ_POINTS * values_per_freq raw values
    // are buffered, the point count provably exceeds the cap, so bail out
    // before allocating any more. (The post-loop check below is retained as
    // defense-in-depth.)
    constexpr size_t MAX_FREQ_POINTS = 10000000;
    const size_t max_raw_values =
        static_cast<size_t>(MAX_FREQ_POINTS) * static_cast<size_t>(values_per_freq);

    while (std::getline(file, line)) {
        // Normalize line endings (CR, LF, CRLF)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::string stripped = stripComment(line);
        // Trim whitespace
        size_t start = stripped.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        size_t end = stripped.find_last_not_of(" \t\r\n");
        stripped = stripped.substr(start, end - start + 1);

        if (stripped.empty())
            continue;

        // Detect version 2.0 keywords
        if (stripped.size() > 10 && stripped[0] == '[') {
            std::string lower = stripped;
            for (char &c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find("[version] 2.0") != std::string::npos) {
                has_version_keyword = true;
            }
            if (has_version_keyword && lower.find("[two-port data order]") != std::string::npos) {
                const size_t value_start = stripped.find(']') + 1;
                std::istringstream order_stream(stripped.substr(value_start));
                std::string order;
                if (!(order_stream >> order))
                    return std::nullopt;
                for (char &c : order)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (order != "12_21" && order != "21_12")
                    return std::nullopt;
                has_two_port_data_order = true;
                two_port_data_order_is_12_21 = order == "12_21";
            }
            if (has_version_keyword) {
                // [Reference] values may continue on following lines until the next keyword.
                in_reference = lower.rfind("[reference]", 0) == 0;
                if (in_reference)
                    appendNumbers(stripped.substr(stripped.find(']') + 1), reference_values);
                continue;
            }
        }
        // Any other bracket line (a short [End] or an unrecognised keyword) is not data.
        if (stripped[0] == '[')
            continue;

        if (stripped[0] == '#') {
            if (!parseOptionLine(stripped, data))
                return std::nullopt;
            option_line_found = true;
            continue;
        }

        if (in_reference) {
            appendNumbers(stripped, reference_values);
            continue;
        }

        // Data line: each whitespace token must be a complete number. Before the option
        // line, reading stops at the first non-numeric token, as it always has, so text
        // preamble lines are still ignored. After it, a bad token rejects the file rather
        // than dropping the rest of the line, which can silently lose a whole point.
        std::istringstream iss(stripped);
        std::string token;
        while (iss >> token) {
            double val;
            if (!parseDataToken(token, val)) {
                if (option_line_found)
                    return std::nullopt;
                break;
            }
            raw_values.push_back(val);
        }

        // R2 in-loop: bail out as soon as the cap is provably exceeded.
        if (raw_values.size() > max_raw_values)
            return std::nullopt;
    }

    // v2.0 [Reference] overrides R. Per-port references are not renormalised, so a
    // list with differing values is rejected rather than read as one reference.
    if (!reference_values.empty()) {
        for (double r : reference_values)
            if (r != reference_values.front()) {
                LOG_WARN("Touchstone [Reference] lists differing per-port impedances, which are "
                         "not supported: %s",
                         filepath.c_str());
                return std::nullopt;
            }
        data.reference_impedance = reference_values.front();
    }

    if (!option_line_found)
        return std::nullopt;

    if (has_version_keyword && data.num_ports == 2 && !has_two_port_data_order)
        return std::nullopt;

    if (raw_values.size() % values_per_freq != 0) {
        // Mismatch: try to be lenient or return null
        return std::nullopt;
    }

    size_t num_freq_points = raw_values.size() / values_per_freq;

    // R2: Defense-in-depth cap (the in-loop check above already bounds the
    // buffer, but keep the explicit check for clarity and safety).
    if (num_freq_points > MAX_FREQ_POINTS)
        return std::nullopt;

    data.frequencies.reserve(num_freq_points);
    data.parameters.reserve(num_freq_points);

    for (size_t i = 0; i < num_freq_points; ++i) {
        size_t base = i * values_per_freq;
        double freq = raw_values[base];
        data.frequencies.push_back(data.freqToHz(freq));

        // Touchstone 2-port order is column-major; 3-port and larger is row-major.
        std::vector<std::complex<double>> file_params;
        file_params.reserve(pairs_per_freq);
        for (int p = 0; p < pairs_per_freq; ++p) {
            double a = raw_values[base + 1 + p * 2];
            double b = raw_values[base + 1 + p * 2 + 1];
            file_params.push_back(parsePair(a, b, data.format));
        }

        if (data.num_ports == 2) {
            if (has_version_keyword && two_port_data_order_is_12_21) {
                data.parameters.push_back(std::move(file_params));
            } else {
                // Touchstone's 2-port exception is S11, S21, S12, S22.
                std::vector<std::complex<double>> rm_params(pairs_per_freq);
                for (int p = 0; p < pairs_per_freq; ++p) {
                    const int row = p % data.num_ports;
                    const int col = p / data.num_ports;
                    rm_params[row * data.num_ports + col] = file_params[p];
                }
                data.parameters.push_back(std::move(rm_params));
            }
        } else {
            data.parameters.push_back(std::move(file_params));
        }
    }

    // R1: Validate frequencies — reject NaN, infinity, negative, non-monotonic
    for (size_t i = 0; i < data.frequencies.size(); ++i) {
        double f = data.frequencies[i];
        if (std::isnan(f) || std::isinf(f) || f < 0.0)
            return std::nullopt;
        if (i > 0 && f <= data.frequencies[i - 1])
            return std::nullopt;
    }

    return data;
}

double TouchstoneData::freqToHz(double freq) const {
    switch (freq_unit) {
    case FrequencyUnit::Hz:
        return freq;
    case FrequencyUnit::kHz:
        return freq * 1e3;
    case FrequencyUnit::MHz:
        return freq * 1e6;
    case FrequencyUnit::GHz:
        return freq * 1e9;
    }
    return freq;
}
