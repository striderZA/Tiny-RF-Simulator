#include "flow_author.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

nlohmann::json buildFlowDocument(const FlowSpec &spec) {
    nlohmann::json root;
    root["version"] = 1;
    if (!spec.name.empty())
        root["name"] = spec.name;

    if (!spec.conditions.empty()) {
        nlohmann::json conditions = nlohmann::json::array();
        for (const Condition &condition : spec.conditions) {
            nlohmann::json values = nlohmann::json::array();
            for (double value : condition.values)
                values.push_back(value);
            conditions.push_back(nlohmann::json{
                {"component", condition.component}, {"path", condition.path}, {"values", values}});
        }
        root["conditions"] = std::move(conditions);
    }

    // `measure` is the one required section, and a flow without it cannot be
    // loaded, so it is always written even when empty — the loader's own
    // "must not be empty" rejection is the answer the author gets, rather than a
    // document whose shape depends on this helper's mood.
    nlohmann::json measure = nlohmann::json::array();
    for (const Measurement &measurement : spec.measure) {
        measure.push_back(nlohmann::json{{"component", measurement.component},
                                         {"port", measurement.port},
                                         {"metric", measurement.metric}});
    }
    root["measure"] = std::move(measure);
    return root;
}

bool parseConditionValues(const std::string &text, std::vector<double> *values,
                          std::string *error) {
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = message;
        return false;
    };

    std::vector<double> parsed;
    const char *cursor = text.c_str();
    while (true) {
        // Skip every separator the field accepts; a mix of them is fine because
        // the author is writing a list, not a grammar.
        while (*cursor == ',' || *cursor == ';' || *cursor == ' ' || *cursor == '\t' ||
               *cursor == '\n' || *cursor == '\r')
            ++cursor;
        if (*cursor == '\0')
            break;

        errno = 0;
        char *end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor)
            return fail("values must be numbers separated by commas or spaces (near '" +
                        std::string(cursor) + "')");
        if (!std::isfinite(value))
            return fail("values must be finite numbers (near '" + std::string(cursor) + "')");
        parsed.push_back(value);
        cursor = end;
    }

    if (parsed.empty())
        return fail("a condition needs at least one value");
    if (values)
        *values = std::move(parsed);
    return true;
}

std::string formatConditionValues(const std::vector<double> &values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0)
            out += ", ";
        // std::to_chars' shortest round-trip form, so a value the author did not
        // touch comes back bit-identical instead of at a printf's precision. It
        // writes "inf"/"nan" for a non-finite value rather than failing, so this
        // error branch is a defensive guard against a buffer too small for a
        // double (unreachable at this size); a non-finite value in a text field is
        // refused by parseConditionValues() on the way back in, not here.
        char buffer[40] = {};
        const std::to_chars_result written =
            std::to_chars(buffer, buffer + sizeof(buffer), values[i]);
        if (written.ec != std::errc()) {
            out += "nan";
            continue;
        }
        out.append(buffer, written.ptr);
    }
    return out;
}

bool writeFlowFile(const std::string &path, const nlohmann::json &document, std::string *error) {
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = message;
        return false;
    };

    const fs::path target(path);
    fs::path temp = target;
    temp += ".tmp";

    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
        return fail("cannot open '" + temp.string() + "' for writing");
    out << document.dump(2) << '\n';
    out.flush();
    if (!out) {
        out.close();
        std::error_code rm_ec;
        fs::remove(temp, rm_ec);
        return fail("write error on '" + temp.string() + "'");
    }
    out.close();
    if (!out) {
        std::error_code rm_ec;
        fs::remove(temp, rm_ec);
        return fail("close error on '" + temp.string() + "'");
    }

    // std::filesystem::rename atomically replaces an existing target on every
    // supported platform, so there is deliberately no remove+rename fallback:
    // deleting the target first would reintroduce the window in which a failure
    // loses a hand-authored flow (the reason ProjectSerializer::save() is
    // atomic too).
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        std::error_code rm_ec;
        fs::remove(temp, rm_ec);
        return fail("cannot replace '" + path + "': " + ec.message());
    }
    return true;
}
