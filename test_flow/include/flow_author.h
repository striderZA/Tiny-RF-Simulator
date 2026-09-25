#pragma once

#include "flow_types.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Authoring helpers for flow files (issue #155): the pieces a UI needs to *write*
// a flow in the same vocabulary the harness reads. They live here rather than in
// the panel so the JSON shape and the value-text grammar are unit-testable
// without ImGui, and so the widget only drives them.

// Builds the flow file `spec` describes: the exact document shape LoadFlowFile()
// reads back — `version` 1, `name` when non-empty, `conditions` only when there
// is one, and `measure`. Nothing is invented: every id and path is copied from
// `spec`, which the caller must have taken from the live circuit (an
// engine's id(), and a path from describeConditionPaths()). The one-way invariant
// this exists for is that a scaffold written this way passes ValidateFlow()
// unchanged, because it is the same FlowSpec the pre-flight already checked.
nlohmann::json buildFlowDocument(const FlowSpec &spec);

// Parses a condition's value list from an authoring text field: numbers separated
// by commas, semicolons, or whitespace ("-30, -20 -10" and "0;1;2" both work).
// Requires at least one value, and rejects a token that is not a complete, finite
// number instead of skipping it — a silently dropped sweep point would produce a
// result that looks complete. Returns false and sets `error` (when non-null).
bool parseConditionValues(const std::string &text, std::vector<double> *values, std::string *error);

// The inverse of parseConditionValues() for the edit field: `std::to_chars`'
// shortest round-trip form, so a value the author did not touch comes back
// bit-identical (`0.1` stays `0.1`, a 17-digit value keeps every digit) and
// editing a value list cannot perturb a value the author did not touch. A
// non-finite value formats as `inf`/`nan`, which parseConditionValues() then
// refuses: that direction of the round trip is deliberately closed, because such
// a value cannot be swept at all.
std::string formatConditionValues(const std::vector<double> &values);

// Writes `document` to `path` as pretty JSON with a trailing newline, through a
// sibling `<path>.tmp` that is renamed over the target only once write, flush and
// close all succeed — the same discipline ProjectSerializer::save() uses for a
// `.rfsim` file (issue #113), because overwriting a hand-authored flow must not
// truncate it when the write fails. Returns false and sets `error` (when
// non-null) on failure; the temp file is removed and the target is left
// byte-identical.
bool writeFlowFile(const std::string &path, const nlohmann::json &document, std::string *error);
