#pragma once

#include <optional>
#include <string>
#include <vector>

// Reader for MathWorks AMP files (RF Toolbox, "AMP File Data Sections"), limited
// to the IIP3 and OIP3 sections.
//
// Read: at most one IIP3 section and one OIP3 section. A section is a header
// `IIP3 [units]` or `OIP3 [units]`, a line `FREQ <unit>`, then either a single
// value line (used at every frequency) or frequency/value rows in any order.
// IP3 units are dBW, dBm, mW, or W and default to W. Frequency units are Hz,
// kHz, MHz, or GHz. A unit may follow a value without a space (`1000MHZ`, `5mW`).
// The IP3 text on the MathWorks page gives header-line defaults only. Per-value suffixes on
// IP3 rows follow the issue's rule that a unit may follow a value.
// Keywords and units are case-insensitive.
//
// Comments: a line whose first non-blank character is `*` or `!` is a comment,
// and `;` starts a trailing comment. Blank lines are ignored.
//
// Skipped: S/Y/Z network, NOI, NF, and POUT sections. Their data is not parsed. S, Y, Z,
// and POUT count toward the file-level rule below only with a numeric data row. An empty
// one is rejected at its header line.
//
// Validity: the file must hold S, Y, Z, or POUT data and at least one IIP3 or
// OIP3 section. Each IP3 kind appears once. Frequencies are positive and unique
// within a section. Values are finite, linear values are positive, and each value
// in dBm lies in [kMinInterceptDbm, kMaxInterceptDbm].
//
// Failures name the file and line: "<path>:<line>: <message>". File-level
// failures omit the line: "<path>: <message>".

// One intercept curve in dBm against frequency in Hz. A single point is a scalar
// that applies at every frequency.
struct AmpIp3Curve {
    struct Point {
        double freq_Hz = 0.0;
        double dBm = 0.0;
    };
    std::vector<Point> points; // non-empty, ascending freq_Hz, no duplicates

    // Linear in frequency between points; held at the nearest point outside them.
    double dBmAt(double freq_Hz) const;
};

struct AmpFile {
    std::optional<AmpIp3Curve> oip3; // output-referred intercept
    std::optional<AmpIp3Curve> iip3; // input-referred intercept
};

struct AmpParseResult {
    std::optional<AmpFile> file; // set on success
    std::string error;           // set on failure
};

class AmpFileParser {
  public:
    // Same range as the inspector's OIP3 field. Values outside it are rejected.
    static constexpr double kMinInterceptDbm = -100.0;
    static constexpr double kMaxInterceptDbm = 200.0;

    static AmpParseResult parse(const std::string &path);
};
