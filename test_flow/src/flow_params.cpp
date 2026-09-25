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

namespace {

// The walk from a snapshot root to the slot a parsed path addresses. Templated
// on the JSON pointer's constness so resolveConditionSlot() can stay a read-only
// API without duplicating this.
template <typename JsonPtr>
JsonPtr walkToSlot(JsonPtr root, const std::vector<Token> &tokens, std::string &error) {
    JsonPtr node = root;
    for (const Token &token : tokens) {
        if (token.is_index) {
            if (!node->is_array()) {
                error = "array index applied to a non-array";
                return nullptr;
            }
            if (token.index >= node->size()) {
                error = "array index " + std::to_string(token.index) + " out of range";
                return nullptr;
            }
            node = &(*node)[token.index];
            continue;
        }
        if (!node->is_object()) {
            error = "key '" + token.key + "' applied to a non-object";
            return nullptr;
        }
        const auto it = node->find(token.key);
        if (it == node->end()) {
            error = "no key '" + token.key + "'";
            return nullptr;
        }
        node = &(*it);
    }
    return node;
}

// `is_number_integer()` is also true for unsigned values, so the unsigned slot
// must be matched first to keep its JSON type.
bool classifySlot(const nlohmann::json &node, ConditionSlotKind *kind, std::string &error) {
    if (node.is_number_unsigned()) {
        *kind = ConditionSlotKind::Unsigned;
        return true;
    }
    if (node.is_number_integer()) {
        *kind = ConditionSlotKind::Signed;
        return true;
    }
    if (node.is_number_float()) {
        *kind = ConditionSlotKind::Float;
        return true;
    }
    error = "slot is not numeric (null, boolean, string, object, or array)";
    return false;
}

} // namespace

bool conditionSlotAccepts(ConditionSlotKind kind, double value, std::string *error) {
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = message;
        return false;
    };

    switch (kind) {
    case ConditionSlotKind::Unsigned:
        if (!std::isfinite(value))
            return fail("slot is unsigned but the value is not finite");
        if (value < 0.0)
            return fail("slot is unsigned but the value is negative");
        if (value != std::floor(value))
            return fail("slot is unsigned but the value is not integral");
        if (value > 1.8e19)
            return fail("value " + std::to_string(value) + " is outside unsigned integer range");
        return true;
    case ConditionSlotKind::Signed:
        if (!std::isfinite(value) || value != std::floor(value))
            return fail("slot is an integer but the value is not integral");
        if (std::abs(value) > 9.0e18)
            return fail("value " + std::to_string(value) + " is outside integer range");
        return true;
    case ConditionSlotKind::Float:
        return std::isfinite(value) ? true : fail("value is not finite");
    }
    return fail("slot is not numeric (null, boolean, string, object, or array)");
}

bool resolveConditionSlot(const nlohmann::json &snapshot, const std::string &path,
                          ConditionSlotKind *kind, std::string *error) {
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = conditionPathErrorPrefix(path) + message;
        return false;
    };

    std::vector<Token> tokens;
    std::string reason;
    if (!parsePath(path, tokens, reason))
        return fail(reason);

    const nlohmann::json *node = walkToSlot(&snapshot, tokens, reason);
    if (node == nullptr)
        return fail(reason);

    ConditionSlotKind resolved = ConditionSlotKind::Float;
    if (!classifySlot(*node, &resolved, reason))
        return fail(reason);
    if (kind != nullptr)
        *kind = resolved;
    return true;
}

bool applyConditionValue(nlohmann::json &snapshot, const std::string &path, double value,
                         std::string *error) {
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = conditionPathErrorPrefix(path) + message;
        return false;
    };

    std::vector<Token> tokens;
    std::string reason;
    if (!parsePath(path, tokens, reason))
        return fail(reason);

    nlohmann::json *node = walkToSlot(&snapshot, tokens, reason);
    if (node == nullptr)
        return fail(reason);

    ConditionSlotKind kind = ConditionSlotKind::Float;
    if (!classifySlot(*node, &kind, reason))
        return fail(reason);
    // The value-side rules live in conditionSlotAccepts() so a sweep can be
    // checked without writing each candidate into the snapshot.
    std::string value_error;
    if (!conditionSlotAccepts(kind, value, &value_error))
        return fail(value_error);

    switch (kind) {
    case ConditionSlotKind::Unsigned:
        *node = static_cast<unsigned long long>(value);
        return true;
    case ConditionSlotKind::Signed:
        *node = static_cast<long long>(value);
        return true;
    case ConditionSlotKind::Float:
        *node = value;
        return true;
    }
    return fail("slot is not numeric (null, boolean, string, object, or array)");
}
