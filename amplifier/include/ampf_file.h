#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Reader for amp-f v1 files. The format is specified in docs/resources/ampf_v1_spec.md.
//
// A file is accepted only when it meets that spec. Each rejection names the file and the
// line, "<path>:<line>: <message>". A problem with the whole file, such as a missing DEVICE
// line, reports "<path>: <message>".
//
// Frequencies are stored in Hz. A missing cell is NaN. Rows are sorted by ascending
// frequency, and rows with equal frequencies keep file order.

// Named values: VAR and COND values, or DEVICE defaults. An absent key is unknown.
using AmpfValues = std::map<std::string, double>;

// One data block. Its identity holds the VAR and COND values after DEVICE defaults are
// inherited. A key absent from the identity is unknown, not zero.
struct AmpfBlock {
    AmpfValues identity;
    int begin_line = 0;                       // line of the block's BEGIN
    std::vector<double> freq_Hz;              // ascending
    std::vector<std::vector<double>> columns; // columns[c][row]; NaN marks missing data

    // Value of column `column` at f_Hz. Interpolates linearly between the two rows that
    // bracket f_Hz and holds the nearest row outside them. NaN when the value, or a
    // bracketing value, is missing.
    double valueAt(size_t column, double f_Hz) const;
};

struct AmpfFile {
    std::string part_number;
    AmpfValues device_defaults;       // DEVICE KEY=VALUE pairs; FREQ_UNITS is not included
    std::vector<std::string> columns; // COLUMNS names, in order
    std::vector<AmpfBlock> blocks;    // file order

    // Index of a COLUMNS name, or -1 when the file has no such column.
    int columnIndex(const std::string &name) const;
};

struct AmpfParseResult {
    std::optional<AmpfFile> file; // set on success
    std::string error;            // set on failure
};

// How many blocks match a selection. One means a single block was selected.
enum class AmpfMatch { None, One, Many };

struct AmpfSelection {
    AmpfMatch match = AmpfMatch::None;
    const AmpfBlock *block = nullptr; // set only for AmpfMatch::One
};

// Selects the blocks whose identity holds every (key, value) pair in `wanted`. Values are
// compared exactly. An empty `wanted` matches every block.
AmpfSelection selectAmpfBlock(const AmpfFile &file, const AmpfValues &wanted);

class AmpfParser {
  public:
    // The file is capped before it is read. Each block is capped in rows.
    static constexpr long long kMaxFileBytes = 256LL * 1024 * 1024;
    static constexpr size_t kMaxBlockRows = 10000000;

    static AmpfParseResult parse(const std::string &path);
};
