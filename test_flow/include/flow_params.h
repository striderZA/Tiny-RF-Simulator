#pragma once

#include <nlohmann/json.hpp>
#include <string>

// The numeric slot a condition path resolved to. It is the only thing a swept
// value has to satisfy:
//   - Signed:   any finite integral value within +/-9e18
//   - Unsigned: a finite, non-negative, integral value within 1.8e19
//   - Float:    any finite value
// Boolean, string, null, object and array slots do not resolve to a kind at all
// (see resolveConditionSlot).
enum class ConditionSlotKind { Signed, Unsigned, Float };

// The prefix every condition-path message carries — "path 'tones[0].power_dBm': "
// — so a caller reporting a *value* rejection (ValidateFlow()) matches
// applyConditionValue()'s wording exactly instead of re-spelling it.
inline std::string conditionPathErrorPrefix(const std::string &path) {
    return "path '" + path + "': ";
}

// True when `value` may be written into a slot of this kind: pure arithmetic, no
// JSON involved, so a caller checking a whole value list pays for the path parse
// once. Returns false and sets `error` (when non-null) with the wording
// applyConditionValue() reports.
bool conditionSlotAccepts(ConditionSlotKind kind, double value, std::string *error);

// Resolves `path` inside `snapshot` to the numeric slot it addresses and reports
// its kind, without writing anything. Returns false and sets `error` (when
// non-null) exactly as applyConditionValue() would for a malformed path, a path
// that does not resolve, or a slot that is not numeric — so
// `resolveConditionSlot(...) && conditionSlotAccepts(kind, value, &error)` holds
// for exactly the values applyConditionValue() would write.
bool resolveConditionSlot(const nlohmann::json &snapshot, const std::string &path,
                          ConditionSlotKind *kind, std::string *error);

// Writes `value` at `path` inside `snapshot`, preserving the JSON type of the
// existing slot: an integer slot requires an integral value and is written back
// as an integer, an unsigned slot requires a non-negative integral value and
// stays unsigned, a float slot is written as a float, and boolean/string/null
// slots are rejected. `path` is a dot/bracket path into the target engine's
// serialize() object, e.g. "gain_dB" or "tones[0].power_dBm".
//
// Returns false and sets `error` (when non-null) if the path is malformed or does
// not resolve to a numeric slot, if the value is non-finite for any numeric slot,
// if a signed integer slot gets a non-integral value or a magnitude above 9e18, or
// if an unsigned slot gets a negative, non-integral, or above-1.8e19 value.
bool applyConditionValue(nlohmann::json &snapshot, const std::string &path, double value,
                         std::string *error);
