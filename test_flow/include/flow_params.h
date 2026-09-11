#pragma once

#include <nlohmann/json.hpp>
#include <string>

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
