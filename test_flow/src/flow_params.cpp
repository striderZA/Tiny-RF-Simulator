#include "flow_params.h"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

struct Token {
    bool is_index = false;
    std::string key;
    size_t index = 0;
};

// Grammar: key ('[' digits ']')* ('.' key ('[' digits ']')*)*
bool parsePath(const std::string &path, std::vector<Token> &tokens, std::string &error) {
    if (path.empty()) {
        error = "path is empty";
        return false;
    }

    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '.') {
            error = "unexpected '.'";
            return false;
        }

        const size_t key_start = i;
        while (i < path.size() && path[i] != '.' && path[i] != '[')
            ++i;
        if (i == key_start) {
            error = "missing key before '['";
            return false;
        }
        tokens.push_back(Token{false, path.substr(key_start, i - key_start), 0});

        while (i < path.size() && path[i] == '[') {
            const size_t close = path.find(']', i);
            if (close == std::string::npos) {
                error = "unterminated '['";
                return false;
            }
            const std::string digits = path.substr(i + 1, close - i - 1);
            if (digits.empty()) {
                error = "empty array index";
                return false;
            }
            for (char c : digits) {
                if (c < '0' || c > '9') {
                    error = "non-numeric array index";
                    return false;
                }
            }
            tokens.push_back(
                Token{true, {}, static_cast<size_t>(std::strtoull(digits.c_str(), nullptr, 10))});
            i = close + 1;
        }

        if (i < path.size()) {
            if (path[i] != '.') {
                error = "malformed path";
                return false;
            }
            ++i;
            if (i == path.size()) {
                error = "path ends with '.'";
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool applyConditionValue(nlohmann::json &snapshot, const std::string &path, double value,
                         std::string *error) {
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = "path '" + path + "': " + message;
        return false;
    };

    std::vector<Token> tokens;
    std::string parse_error;
    if (!parsePath(path, tokens, parse_error))
        return fail(parse_error);

    nlohmann::json *node = &snapshot;
    for (const Token &token : tokens) {
        if (token.is_index) {
            if (!node->is_array())
                return fail("array index applied to a non-array");
            if (token.index >= node->size())
                return fail("array index " + std::to_string(token.index) + " out of range");
            node = &(*node)[token.index];
            continue;
        }
        if (!node->is_object())
            return fail("key '" + token.key + "' applied to a non-object");
        const auto it = node->find(token.key);
        if (it == node->end())
            return fail("no key '" + token.key + "'");
        node = &(*it);
    }

    if (node->is_number_integer() || node->is_number_unsigned()) {
        if (!std::isfinite(value) || value != std::floor(value))
            return fail("slot is an integer but the value is not integral");
        if (std::abs(value) > 9.0e18)
            return fail("value " + std::to_string(value) + " is outside integer range");
        *node = static_cast<long long>(value);
        return true;
    }
    if (node->is_number_float()) {
        if (!std::isfinite(value))
            return fail("value is not finite");
        *node = value;
        return true;
    }
    return fail("slot is not numeric (null, boolean, string, object, or array)");
}
