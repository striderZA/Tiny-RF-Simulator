#include "ampf_file.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr const char *kMagicLine = "# amp-f v1";

struct Diagnostic {
    int line = 0; // 0: the problem concerns the whole file
    std::string message;
};

using Error = std::optional<Diagnostic>;

Error fail(int line, std::string message) { return Diagnostic{line, std::move(message)}; }

AmpfParseResult failure(const std::string &path, const Diagnostic &diagnostic) {
    AmpfParseResult result;
    result.error = diagnostic.line > 0
                       ? path + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message
                       : path + ": " + diagnostic.message;
    return result;
}

bool isBlank(char c) { return c == ' ' || c == '\t'; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

std::string trim(const std::string &text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isBlank(text[begin]))
        ++begin;
    while (end > begin && isBlank(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

// Splits on runs of spaces and tabs.
std::vector<std::string> split(const std::string &text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isBlank(text[i]))
            ++i;
        const size_t start = i;
        while (i < text.size() && !isBlank(text[i]))
            ++i;
        if (i > start)
            tokens.push_back(text.substr(start, i - start));
    }
    return tokens;
}

// A decimal number: an optional sign, digits with an optional fraction, and an optional
// exponent. The value must be finite, so overflow to infinity is rejected.
bool parseDecimal(const std::string &token, double &value) {
    const size_t n = token.size();
    size_t i = 0;
    if (i < n && (token[i] == '+' || token[i] == '-'))
        ++i;
    size_t digits = 0;
    while (i < n && isDigit(token[i])) {
        ++i;
        ++digits;
    }
    if (i < n && token[i] == '.') {
        ++i;
        while (i < n && isDigit(token[i])) {
            ++i;
            ++digits;
        }
    }
    if (digits == 0)
        return false;
    if (i < n && (token[i] == 'e' || token[i] == 'E')) {
        ++i;
        if (i < n && (token[i] == '+' || token[i] == '-'))
            ++i;
        size_t exponent_digits = 0;
        while (i < n && isDigit(token[i])) {
            ++i;
            ++exponent_digits;
        }
        if (exponent_digits == 0)
            return false;
    }
    if (i != n)
        return false;
    // std::from_chars ignores the C numeric locale, so a comma-decimal locale cannot read
    // "2.5" as 2. It takes no leading '+', which the grammar above allows, so drop it first.
    std::string_view text(token);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    const char *end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    return ec == std::errc() && ptr == end && std::isfinite(value);
}

// A KEY name: a letter, then letters, digits, or underscores.
bool isKeyName(const std::string &text) {
    if (text.empty() || !isLetter(text[0]))
        return false;
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return isLetter(c) || isDigit(c) || c == '_'; });
}

// Splits KEY=VALUE at its only '='. Returns false for a malformed pair. The caller checks
// the value text.
bool splitPair(const std::string &token, std::string &key, std::string &value_text) {
    const size_t eq = token.find('=');
    if (eq == std::string::npos)
        return false;
    key = token.substr(0, eq);
    value_text = token.substr(eq + 1);
    return isKeyName(key) && !value_text.empty() && value_text.find('=') == std::string::npos;
}

// Scale from a FREQ_UNITS value to Hz. Only the exact spellings are accepted.
bool frequencyScale(const std::string &unit, double &scale) {
    if (unit == "Hz")
        scale = 1.0;
    else if (unit == "kHz")
        scale = 1.0e3;
    else if (unit == "MHz")
        scale = 1.0e6;
    else if (unit == "GHz")
        scale = 1.0e9;
    else
        return false;
    return true;
}

// VAR and COND lines collected ahead of a BEGIN.
struct Preamble {
    AmpfValues vars;
    AmpfValues conds;
    int first_line = 0; // 0: no VAR or COND line yet
};

// A data block between BEGIN and END. Rows are kept in file order until the block closes.
struct OpenBlock {
    int begin_line = 0;
    AmpfValues vars;
    AmpfValues conds;
    std::vector<double> freq_Hz;
    std::vector<std::vector<double>> columns;
};

struct ParseState {
    AmpfFile file;
    bool have_device = false;
    bool have_columns = false;
    bool block_started = false; // a VAR, COND, or BEGIN line has been read
    double freq_scale = 1.0e6;  // MHz unless FREQ_UNITS says otherwise
    bool freq_units_set = false;
    std::map<std::string, int> var_lines;  // VAR key -> line of its first VAR line
    std::map<std::string, int> cond_lines; // COND key -> line of its first COND line
    std::map<AmpfValues, int> identities;  // block identity -> line of its BEGIN
    Preamble preamble;
    bool in_data = false;
    OpenBlock open;
};

// Handles "# COLUMNS: names". The names end at a '#'.
Error handleColumns(ParseState &s, int line, const std::string &names_text) {
    if (s.have_columns)
        return fail(line, "duplicate # COLUMNS: comment");
    if (s.block_started)
        return fail(line, "# COLUMNS: must precede the first data block");
    const size_t hash = names_text.find('#');
    const auto names = split(hash == std::string::npos ? names_text : names_text.substr(0, hash));
    if (names.empty())
        return fail(line, "# COLUMNS: lists no columns");
    std::set<std::string> seen;
    for (const auto &name : names) {
        if (!seen.insert(name).second)
            return fail(line, "# COLUMNS: repeats column '" + name + "'");
    }
    s.file.columns = names;
    s.have_columns = true;
    return std::nullopt;
}

// Handles the DEVICE line: a part number, then KEY=VALUE pairs. FREQ_UNITS sets the scale of
// the whole file and is not kept as a default.
Error handleDevice(ParseState &s, int line, const std::vector<std::string> &tokens) {
    if (s.have_device)
        return fail(line, "duplicate DEVICE line");
    if (tokens.size() < 2)
        return fail(line, "DEVICE needs a part number");
    s.file.part_number = tokens[1];
    for (size_t i = 2; i < tokens.size(); ++i) {
        std::string key;
        std::string value_text;
        if (!splitPair(tokens[i], key, value_text))
            return fail(line, "'" + tokens[i] + "' is not a KEY=VALUE pair");
        if (key == "FREQ_UNITS") {
            if (s.freq_units_set)
                return fail(line, "DEVICE repeats key 'FREQ_UNITS'");
            double scale = 0.0;
            if (!frequencyScale(value_text, scale))
                return fail(line, "FREQ_UNITS must be Hz, kHz, MHz, or GHz");
            s.freq_scale = scale;
            s.freq_units_set = true;
            continue;
        }
        if (s.file.device_defaults.count(key))
            return fail(line, "DEVICE repeats key '" + key + "'");
        double value = 0.0;
        if (!parseDecimal(value_text, value))
            return fail(line, "DEVICE value '" + value_text + "' is not a decimal number");
        s.file.device_defaults[key] = value;
    }
    s.have_device = true;
    return std::nullopt;
}

// Handles a VAR or COND line in a block's preamble.
Error handlePreamble(ParseState &s, int line, const std::vector<std::string> &tokens) {
    const bool is_var = tokens[0] == "VAR";
    const std::string keyword = is_var ? "VAR" : "COND";
    if (!s.have_device || !s.have_columns)
        return fail(line, "DEVICE and COLUMNS must precede the first data block");
    s.block_started = true;
    if (tokens.size() != 2)
        return fail(line, keyword + " takes one KEY=VALUE pair");
    std::string key;
    std::string value_text;
    if (!splitPair(tokens[1], key, value_text))
        return fail(line, "'" + tokens[1] + "' is not a KEY=VALUE pair");
    if (key == "FREQ_UNITS")
        return fail(line, "FREQ_UNITS is only valid on the DEVICE line");
    double value = 0.0;
    if (!parseDecimal(value_text, value))
        return fail(line,
                    keyword + " '" + key + "' value '" + value_text + "' is not a decimal number");

    if (is_var) {
        if (s.preamble.vars.count(key))
            return fail(line, "VAR '" + key + "' repeats a key in this block");
        if (s.preamble.vars.size() == 2)
            return fail(line, "a block takes at most two VAR lines");
        if (s.file.device_defaults.count(key))
            return fail(line, "'" + key + "' is a DEVICE key and cannot be a VAR key (rule 6)");
        const auto cond = s.cond_lines.find(key);
        if (cond != s.cond_lines.end())
            return fail(line, "'" + key + "' is a COND key on line " +
                                  std::to_string(cond->second) +
                                  " and cannot be a VAR key (rule 7)");
        s.var_lines.emplace(key, line);
        s.preamble.vars[key] = value;
    } else {
        if (s.preamble.conds.count(key))
            return fail(line, "COND '" + key + "' repeats a key in this block");
        const auto var = s.var_lines.find(key);
        if (var != s.var_lines.end())
            return fail(line, "'" + key + "' is a VAR key on line " + std::to_string(var->second) +
                                  " and cannot be a COND key (rule 7)");
        s.cond_lines.emplace(key, line);
        s.preamble.conds[key] = value;
    }
    if (s.preamble.first_line == 0)
        s.preamble.first_line = line;
    return std::nullopt;
}

// Handles BEGIN. The collected preamble becomes this block's preamble.
Error handleBegin(ParseState &s, int line, const std::vector<std::string> &tokens) {
    if (tokens.size() != 1)
        return fail(line, "BEGIN takes no arguments");
    if (!s.have_device)
        return fail(line, "no DEVICE line before the first data block");
    if (!s.have_columns)
        return fail(line, "no # COLUMNS: comment before the first data block");
    s.block_started = true;
    s.in_data = true;
    s.open = OpenBlock{};
    s.open.begin_line = line;
    s.open.vars = std::move(s.preamble.vars);
    s.open.conds = std::move(s.preamble.conds);
    s.open.columns.assign(s.file.columns.size(), std::vector<double>());
    s.preamble = Preamble{};
    return std::nullopt;
}

// Handles one data row between BEGIN and END. The caller has already routed END.
Error handleDataRow(ParseState &s, int line, const std::string &raw) {
    if (raw.find('#') != std::string::npos)
        return fail(line, "comments are not allowed between BEGIN and END");
    const auto tokens = split(raw);
    if (tokens.empty())
        return fail(line, "blank lines are not allowed between BEGIN and END");
    const std::string &first = tokens[0];
    if (first == "BEGIN")
        return fail(line, "BEGIN inside a data block: blocks cannot nest");
    if (first == "DEVICE" || first == "VAR" || first == "COND")
        return fail(line, first + " cannot appear between BEGIN and END");

    const size_t columns = s.file.columns.size();
    if (tokens.size() != columns + 1)
        return fail(line, "data row has " + std::to_string(tokens.size()) + " fields; expected " +
                              std::to_string(columns + 1));
    double freq = 0.0;
    if (!parseDecimal(first, freq))
        return fail(line, "frequency '" + first + "' is not a decimal number");
    const double freq_Hz = freq * s.freq_scale;
    if (!std::isfinite(freq_Hz))
        return fail(line, "frequency '" + first + "' is out of range");

    std::vector<double> values(columns, 0.0);
    for (size_t c = 0; c < columns; ++c) {
        const std::string &token = tokens[c + 1];
        if (token == "NaN")
            values[c] = std::numeric_limits<double>::quiet_NaN();
        else if (!parseDecimal(token, values[c]))
            return fail(line, "value '" + token + "' is not a decimal number or NaN");
    }
    if (s.open.freq_Hz.size() >= AmpfParser::kMaxBlockRows)
        return fail(line, "block exceeds the " + std::to_string(AmpfParser::kMaxBlockRows) +
                              "-row limit");
    s.open.freq_Hz.push_back(freq_Hz);
    for (size_t c = 0; c < columns; ++c)
        s.open.columns[c].push_back(values[c]);
    return std::nullopt;
}

// Closes the open block at END. Checks that it has rows and a unique identity, then stores
// it with its rows sorted by frequency.
Error closeBlock(ParseState &s) {
    OpenBlock &block = s.open;
    if (block.freq_Hz.empty())
        return fail(block.begin_line, "block has no data rows");

    // Identity: VAR and COND values, with DEVICE defaults inherited for keys no COND line sets.
    AmpfValues identity = block.vars;
    for (const auto &[key, value] : block.conds)
        identity[key] = value;
    for (const auto &[key, value] : s.file.device_defaults) {
        if (!block.conds.count(key))
            identity.emplace(key, value);
    }
    const auto [seen, inserted] = s.identities.emplace(identity, block.begin_line);
    if (!inserted)
        return fail(block.begin_line, "block duplicates the identity of the block at line " +
                                          std::to_string(seen->second));

    const size_t n = block.freq_Hz.size();
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return block.freq_Hz[a] < block.freq_Hz[b]; });

    AmpfBlock out;
    out.identity = std::move(identity);
    out.begin_line = block.begin_line;
    out.freq_Hz.resize(n);
    for (size_t i = 0; i < n; ++i)
        out.freq_Hz[i] = block.freq_Hz[order[i]];
    out.columns.assign(block.columns.size(), std::vector<double>(n, 0.0));
    for (size_t c = 0; c < block.columns.size(); ++c) {
        for (size_t i = 0; i < n; ++i)
            out.columns[c][i] = block.columns[c][order[i]];
    }
    s.file.blocks.push_back(std::move(out));

    s.in_data = false;
    s.open = OpenBlock{};
    return std::nullopt;
}

// Handles one line outside a data block.
Error handleOutsideLine(ParseState &s, int line, const std::string &raw) {
    const std::string text = trim(raw);
    if (text.empty())
        return std::nullopt;
    if (text[0] == '#') {
        const std::string body = trim(text.substr(1));
        const std::string directive = "COLUMNS:";
        if (body.rfind(directive, 0) == 0)
            return handleColumns(s, line, body.substr(directive.size()));
        return std::nullopt; // any other comment
    }

    const size_t hash = text.find('#');
    const auto tokens = split(hash == std::string::npos ? text : text.substr(0, hash));
    if (tokens.empty())
        return std::nullopt;
    const std::string &keyword = tokens[0];
    if (keyword == "DEVICE")
        return handleDevice(s, line, tokens);
    if (keyword == "VAR" || keyword == "COND")
        return handlePreamble(s, line, tokens);
    if (keyword == "BEGIN")
        return handleBegin(s, line, tokens);
    if (keyword == "END")
        return fail(line, "END without a matching BEGIN");
    double ignored = 0.0;
    if (parseDecimal(keyword, ignored))
        return fail(line, "data row outside BEGIN/END");
    return fail(line, "unrecognized keyword '" + keyword + "'");
}

// Handles one line. END closes the open block; any other line inside a block is a data row.
Error handleLine(ParseState &s, int line, const std::string &raw) {
    if (!s.in_data)
        return handleOutsideLine(s, line, raw);
    const auto tokens = split(raw);
    if (raw.find('#') == std::string::npos && !tokens.empty() && tokens[0] == "END") {
        if (tokens.size() != 1)
            return fail(line, "END takes no arguments");
        return closeBlock(s);
    }
    return handleDataRow(s, line, raw);
}

// Checks the file as a whole once every line has been read.
Error finishFile(ParseState &s) {
    if (s.in_data)
        return fail(s.open.begin_line, "BEGIN without END");
    if (s.preamble.first_line > 0)
        return fail(s.preamble.first_line, "VAR or COND lines without a BEGIN");
    if (!s.have_device)
        return fail(0, "no DEVICE line");
    if (!s.have_columns)
        return fail(0, "no # COLUMNS: comment");
    return std::nullopt;
}

} // namespace

double AmpfBlock::valueAt(size_t column, double f_Hz) const {
    const double missing = std::numeric_limits<double>::quiet_NaN();
    if (column >= columns.size() || freq_Hz.empty())
        return missing;
    const auto &values = columns[column];
    if (f_Hz <= freq_Hz.front())
        return values.front();
    if (f_Hz >= freq_Hz.back())
        return values.back();

    const auto upper = std::lower_bound(freq_Hz.begin(), freq_Hz.end(), f_Hz);
    const size_t i = static_cast<size_t>(upper - freq_Hz.begin());
    if (*upper == f_Hz)
        return values[i];
    const double y0 = values[i - 1];
    const double y1 = values[i];
    if (!std::isfinite(y0) || !std::isfinite(y1))
        return missing;
    const double x0 = freq_Hz[i - 1];
    const double x1 = freq_Hz[i];
    return y0 + (y1 - y0) * (f_Hz - x0) / (x1 - x0);
}

int AmpfFile::columnIndex(const std::string &name) const {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

AmpfSelection selectAmpfBlock(const AmpfFile &file, const AmpfValues &wanted) {
    AmpfSelection selection;
    int matches = 0;
    for (const AmpfBlock &block : file.blocks) {
        const bool all = std::all_of(wanted.begin(), wanted.end(), [&](const auto &pair) {
            const auto found = block.identity.find(pair.first);
            return found != block.identity.end() && found->second == pair.second;
        });
        if (!all)
            continue;
        ++matches;
        selection.block = &block;
    }
    if (matches == 0) {
        selection.block = nullptr;
        return selection;
    }
    if (matches == 1) {
        selection.match = AmpfMatch::One;
        return selection;
    }
    selection.match = AmpfMatch::Many;
    selection.block = nullptr;
    return selection;
}

AmpfParseResult AmpfParser::parse(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return failure(path, {0, "cannot open file"});
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || size > kMaxFileBytes)
        return failure(path, {0, "file exceeds the 256 MiB amp-f size limit"});
    file.seekg(0, std::ios::beg);

    ParseState state;
    std::string raw;
    int line = 0;
    while (std::getline(file, raw)) {
        ++line;
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back(); // CRLF line endings
        if (line == 1) {
            if (raw != kMagicLine)
                return failure(path, {1, "line 1 must be exactly '# amp-f v1'"});
            continue;
        }
        if (auto error = handleLine(state, line, raw))
            return failure(path, *error);
    }
    if (line == 0)
        return failure(path, {1, "line 1 must be exactly '# amp-f v1'"});
    if (auto error = finishFile(state))
        return failure(path, *error);

    AmpfParseResult result;
    result.file = std::move(state.file);
    return result;
}
