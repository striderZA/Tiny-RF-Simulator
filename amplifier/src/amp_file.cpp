#include "amp_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

// Guards match TouchstoneParser: the file is capped before it is read, and one
// section is capped in points.
constexpr std::streamoff kMaxFileBytes = 256LL * 1024 * 1024;
constexpr size_t kMaxSectionPoints = 10000000;

struct Diagnostic {
    int line = 0; // 0: the problem concerns the whole file
    std::string message;
};

AmpParseResult failure(const std::string &path, const Diagnostic &diagnostic) {
    AmpParseResult result;
    result.error = diagnostic.line > 0
                       ? path + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message
                       : path + ": " + diagnostic.message;
    return result;
}

std::string lowerCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isCommentLine(const std::string &line) {
    const auto first = line.find_first_not_of(" \t\r");
    return first != std::string::npos && (line[first] == '*' || line[first] == '!');
}

// Drops a trailing ';' comment and splits the rest on whitespace.
std::vector<std::string> tokenize(std::string line) {
    const auto semicolon = line.find(';');
    if (semicolon != std::string::npos)
        line.erase(semicolon);
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;)
        tokens.push_back(token);
    return tokens;
}

// Splits "1000MHZ" into 1000 and "MHZ". The number is a plain decimal: an optional
// sign, digits with an optional fraction, and an optional exponent. An exponent
// without digits stays in the suffix. Non-finite values are rejected.
bool splitNumber(const std::string &token, double &value, std::string &suffix) {
    const size_t n = token.size();
    const auto isDigit = [&token, n](size_t k) {
        return k < n && std::isdigit(static_cast<unsigned char>(token[k])) != 0;
    };
    size_t i = 0;
    if (i < n && (token[i] == '+' || token[i] == '-'))
        ++i;
    size_t digits = 0;
    while (isDigit(i)) {
        ++i;
        ++digits;
    }
    if (i < n && token[i] == '.') {
        ++i;
        while (isDigit(i)) {
            ++i;
            ++digits;
        }
    }
    if (digits == 0)
        return false;
    if (i < n && (token[i] == 'e' || token[i] == 'E')) {
        size_t j = i + 1;
        if (j < n && (token[j] == '+' || token[j] == '-'))
            ++j;
        if (isDigit(j)) {
            while (isDigit(j))
                ++j;
            i = j;
        }
    }
    const std::string number = token.substr(0, i);
    char *end = nullptr;
    value = std::strtod(number.c_str(), &end);
    if (end != number.c_str() + number.size() || !std::isfinite(value))
        return false;
    suffix = token.substr(i);
    return true;
}

// Scale from a frequency unit to Hz. Returns false, and leaves scale unchanged, for
// an unknown unit.
bool frequencyScale(const std::string &unit, double &scale) {
    const std::string u = lowerCase(unit);
    if (u == "hz")
        scale = 1.0;
    else if (u == "khz")
        scale = 1e3;
    else if (u == "mhz")
        scale = 1e6;
    else if (u == "ghz")
        scale = 1e9;
    else
        return false;
    return true;
}

bool isIp3Unit(const std::string &lower) {
    return lower == "dbw" || lower == "dbm" || lower == "mw" || lower == "w";
}

// Converts a value in a valid IP3 unit (lowercase) to dBm. The caller has already
// checked that linear values are positive.
double toDbm(double value, const std::string &lower_unit) {
    if (lower_unit == "dbm")
        return value;
    if (lower_unit == "dbw")
        return value + 30.0;
    const double milliwatts = lower_unit == "w" ? value * 1000.0 : value;
    return 10.0 * std::log10(milliwatts);
}

// Parses one IP3 value: a number with an optional unit suffix. The section's
// default unit applies when the token has no suffix.
std::optional<Diagnostic> parseIp3Value(const std::string &token, const std::string &default_unit,
                                        int line, double &dbm) {
    double value = 0.0;
    std::string suffix;
    if (!splitNumber(token, value, suffix))
        return Diagnostic{line, "expected a finite IP3 value, got '" + token + "'"};
    const std::string unit = suffix.empty() ? default_unit : lowerCase(suffix);
    if (!isIp3Unit(unit))
        return Diagnostic{line, "unknown IP3 units '" + suffix + "' (expected dBW, dBm, mW, or W)"};
    if ((unit == "w" || unit == "mw") && !(value > 0.0))
        return Diagnostic{line, "linear IP3 values must be positive"};
    dbm = toDbm(value, unit);
    if (!std::isfinite(dbm) || dbm < AmpFileParser::kMinInterceptDbm ||
        dbm > AmpFileParser::kMaxInterceptDbm)
        return Diagnostic{line, "IP3 value is outside -100..200 dBm"};
    return std::nullopt;
}

std::optional<Diagnostic> parseFrequency(const std::string &token, double default_scale, int line,
                                         double &freq_Hz) {
    double value = 0.0;
    std::string suffix;
    if (!splitNumber(token, value, suffix))
        return Diagnostic{line, "expected a finite frequency, got '" + token + "'"};
    double scale = default_scale;
    if (!suffix.empty() && !frequencyScale(suffix, scale))
        return Diagnostic{line, "unknown frequency unit '" + suffix + "'"};
    freq_Hz = value * scale;
    if (!std::isfinite(freq_Hz) || freq_Hz <= 0.0)
        return Diagnostic{line, "frequency must be positive"};
    return std::nullopt;
}

enum class Keyword { None, Iip3, Oip3, NetworkOrPower, Noise };

Keyword classify(const std::string &token) {
    const std::string k = lowerCase(token);
    if (k == "iip3")
        return Keyword::Iip3;
    if (k == "oip3")
        return Keyword::Oip3;
    if (k == "s" || k == "sparameters" || k == "y" || k == "yparameters" || k == "z" ||
        k == "zparameters" || k == "pout")
        return Keyword::NetworkOrPower;
    if (k == "noi" || k == "noise" || k == "nf")
        return Keyword::Noise;
    return Keyword::None;
}

const char *ip3Name(bool is_oip3) { return is_oip3 ? "OIP3" : "IIP3"; }

struct Ip3Section {
    bool is_oip3 = false;
    int header_line = 0;
    std::string value_unit = "w"; // lowercase; W is the format default
    int frequency_line = 0;
    std::string frequency_unit; // as written on the FREQ line
    struct Row {
        int line = 0;
        std::vector<std::string> tokens;
    };
    std::vector<Row> rows;
};

// Builds the curve for a finished section, or reports its first problem.
std::optional<Diagnostic> buildCurve(const Ip3Section &section, AmpIp3Curve &curve) {
    if (section.rows.empty())
        return Diagnostic{section.header_line,
                          std::string(ip3Name(section.is_oip3)) + " section has no data"};
    const size_t shape = section.rows.front().tokens.size();
    if (shape != 1 && shape != 2)
        return Diagnostic{section.rows.front().line,
                          "expected a frequency and a value, or a single value"};
    for (const auto &row : section.rows) {
        if (row.tokens.size() != shape)
            return Diagnostic{row.line,
                              "rows must all hold a frequency and a value, or all hold one value"};
    }

    if (shape == 1) {
        if (section.rows.size() > 1)
            return Diagnostic{section.rows[1].line, "a scalar IP3 section takes one value line"};
        double dbm = 0.0;
        if (auto error = parseIp3Value(section.rows.front().tokens.front(), section.value_unit,
                                       section.rows.front().line, dbm))
            return error;
        curve.points = {{0.0, dbm}};
        return std::nullopt;
    }

    double default_scale = 0.0;
    if (!frequencyScale(section.frequency_unit, default_scale)) {
        if (section.frequency_unit.empty())
            return Diagnostic{section.frequency_line,
                              "FREQ header needs a unit (GHz, MHz, kHz, or Hz)"};
        return Diagnostic{section.frequency_line,
                          "unknown frequency unit '" + section.frequency_unit + "'"};
    }

    struct Parsed {
        double freq_Hz;
        double dBm;
        int line;
    };
    std::vector<Parsed> parsed;
    parsed.reserve(section.rows.size());
    for (const auto &row : section.rows) {
        Parsed point{0.0, 0.0, row.line};
        if (auto error = parseFrequency(row.tokens[0], default_scale, row.line, point.freq_Hz))
            return error;
        if (auto error = parseIp3Value(row.tokens[1], section.value_unit, row.line, point.dBm))
            return error;
        parsed.push_back(point);
    }
    std::stable_sort(parsed.begin(), parsed.end(),
                     [](const Parsed &a, const Parsed &b) { return a.freq_Hz < b.freq_Hz; });
    for (size_t i = 1; i < parsed.size(); ++i) {
        if (parsed[i].freq_Hz == parsed[i - 1].freq_Hz)
            return Diagnostic{parsed[i].line, "duplicate frequency"};
    }
    curve.points.clear();
    for (const auto &point : parsed)
        curve.points.push_back({point.freq_Hz, point.dBm});
    return std::nullopt;
}

} // namespace

double AmpIp3Curve::dBmAt(double freq_Hz) const {
    if (points.size() == 1 || !(freq_Hz > points.front().freq_Hz))
        return points.front().dBm;
    if (freq_Hz >= points.back().freq_Hz)
        return points.back().dBm;
    const auto upper = std::lower_bound(points.begin(), points.end(), freq_Hz,
                                        [](const Point &p, double f) { return p.freq_Hz < f; });
    const auto lower = upper - 1;
    const double t = (freq_Hz - lower->freq_Hz) / (upper->freq_Hz - lower->freq_Hz);
    return lower->dBm + t * (upper->dBm - lower->dBm);
}

AmpParseResult AmpFileParser::parse(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return failure(path, {0, "cannot open file"});
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || size > kMaxFileBytes)
        return failure(path, {0, "file exceeds the 256 MiB AMP size limit"});
    file.seekg(0, std::ios::beg);

    // BeforeSection: no header yet. AwaitFrequencyHeader: an IP3 header was read and
    // its FREQ line is due. Rows: IP3 data. Skipping: an S/Y/Z/NOI/NF/POUT section.
    enum class Mode { BeforeSection, AwaitFrequencyHeader, Rows, Skipping };
    Mode mode = Mode::BeforeSection;
    Ip3Section section;
    AmpFile amp;
    bool seen_iip3 = false;
    bool seen_oip3 = false;
    bool seen_network_or_power = false;

    const auto closeSection = [&]() -> std::optional<Diagnostic> {
        AmpIp3Curve curve;
        if (auto error = buildCurve(section, curve))
            return error;
        if (section.is_oip3)
            amp.oip3 = std::move(curve);
        else
            amp.iip3 = std::move(curve);
        return std::nullopt;
    };

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        if (isCommentLine(line))
            continue;
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty())
            continue;

        const Keyword keyword = classify(tokens.front());
        if (keyword != Keyword::None) {
            // A new header ends the section before it.
            if (mode == Mode::AwaitFrequencyHeader) {
                return failure(path,
                               {section.header_line, std::string(ip3Name(section.is_oip3)) +
                                                         " section needs a FREQ header line"});
            }
            if (mode == Mode::Rows) {
                if (auto error = closeSection())
                    return failure(path, *error);
            }

            if (keyword == Keyword::Iip3 || keyword == Keyword::Oip3) {
                const bool is_oip3 = keyword == Keyword::Oip3;
                if (is_oip3 ? seen_oip3 : seen_iip3)
                    return failure(
                        path, {line_no, std::string("duplicate ") + ip3Name(is_oip3) + " section"});
                (is_oip3 ? seen_oip3 : seen_iip3) = true;
                if (tokens.size() > 2) {
                    return failure(path, {line_no, std::string("unexpected text after the ") +
                                                       ip3Name(is_oip3) + " units"});
                }
                const std::string unit =
                    tokens.size() == 2 ? lowerCase(tokens[1]) : std::string("w");
                if (!isIp3Unit(unit)) {
                    return failure(path, {line_no, "unknown IP3 units '" + tokens[1] +
                                                       "' (expected dBW, dBm, mW, or W)"});
                }
                section = Ip3Section{};
                section.is_oip3 = is_oip3;
                section.header_line = line_no;
                section.value_unit = unit;
                mode = Mode::AwaitFrequencyHeader;
            } else {
                seen_network_or_power = seen_network_or_power || keyword == Keyword::NetworkOrPower;
                mode = Mode::Skipping;
            }
            continue;
        }

        switch (mode) {
        case Mode::BeforeSection:
            return failure(path, {line_no, "data before a section header (IIP3, OIP3, S, Y, Z, "
                                           "NOI, NF, or POUT)"});
        case Mode::Skipping:
            break;
        case Mode::AwaitFrequencyHeader:
            if (lowerCase(tokens.front()) != "freq") {
                return failure(path,
                               {line_no, std::string("expected a FREQ header line after the ") +
                                             ip3Name(section.is_oip3) + " header"});
            }
            if (tokens.size() > 2)
                return failure(path, {line_no, "unexpected text after the FREQ unit"});
            section.frequency_line = line_no;
            section.frequency_unit = tokens.size() == 2 ? tokens[1] : std::string();
            mode = Mode::Rows;
            break;
        case Mode::Rows:
            if (section.rows.size() >= kMaxSectionPoints)
                return failure(path, {line_no, "too many points in one IP3 section"});
            if (tokens.size() > 2)
                return failure(path,
                               {line_no, "expected a frequency and a value, or a single value"});
            section.rows.push_back({line_no, tokens});
            break;
        }
    }

    if (mode == Mode::AwaitFrequencyHeader) {
        return failure(path, {section.header_line,
                              std::string(ip3Name(section.is_oip3)) + " section has no data"});
    }
    if (mode == Mode::Rows) {
        if (auto error = closeSection())
            return failure(path, *error);
    }
    if (!seen_network_or_power)
        return failure(path, {0, "no S, Y, Z, or POUT section; an AMP file needs network or "
                                 "power data"});
    if (!amp.iip3 && !amp.oip3)
        return failure(path, {0, "no IIP3 or OIP3 section"});

    AmpParseResult result;
    result.file = std::move(amp);
    return result;
}
