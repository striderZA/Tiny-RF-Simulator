#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// RF budget model (Plan 2). A stage table describes the topology: each stage
// lists the feed of each input port, and outputs name (stage, port) pairs.
// Stage names are the internal keys. The model is UI-free and lives in
// `simulator::editor_services`.
constexpr int kRfBudgetFormat = 1;
constexpr std::size_t kMaxRfBudgetStages = 32;
constexpr std::size_t kMaxRfBudgetOutputs = 8;
constexpr std::size_t kMaxRfBudgetModeRecords = 64;
constexpr int kMinRfBudgetPoints = 2;
constexpr int kMaxRfBudgetPoints = 401;
constexpr int kDefaultRfBudgetPoints = 201;

// The analog, non-converting component types a budget can hold, with the pin
// counts their engines report.
struct RfBudgetStageType {
    std::string_view type;
    int inputs;
    int outputs;
};
const std::vector<RfBudgetStageType> &rfBudgetStageTypes();

enum class RfBudgetStageSource { Library, Idealized };

struct RfBudgetFeed {
    std::optional<std::string> stage; // nullopt: fed from the budget input
    int port = 0;
    bool operator==(const RfBudgetFeed &) const = default;
};

struct RfBudgetStage {
    std::string name;
    std::string type;
    RfBudgetStageSource source = RfBudgetStageSource::Idealized;
    std::string part_number;        // Library only
    nlohmann::ordered_json params = // Idealized only
        nlohmann::ordered_json::object();
    std::vector<std::optional<RfBudgetFeed>> inputs; // one per input port; nullopt = unfed
    // Params compare by content: `ordered_json` equality is key-order sensitive, and a
    // JSON round trip through the unordered project document cannot preserve the order.
    bool operator==(const RfBudgetStage &other) const;
};

struct RfBudgetOutput {
    std::string name;
    std::string stage;
    int port = 0;
    bool operator==(const RfBudgetOutput &) const = default;
};

struct RfBudgetModeRecord {
    std::uint32_t mode_id = 0;
    std::map<std::string, std::map<std::string, double>> overrides;
    bool operator==(const RfBudgetModeRecord &) const = default;
};

struct RfBudget {
    int point_count = kDefaultRfBudgetPoints;
    std::vector<RfBudgetStage> stages;
    std::vector<RfBudgetOutput> outputs;
    std::vector<RfBudgetModeRecord> modes;
    std::string invalid_reason; // set only for retained malformed project data
    bool empty() const;
    bool operator==(const RfBudget &) const = default;
};

// The first refusal, or nullopt. Messages are exact and name the offending
// stage or output where the brief requires it.
std::optional<std::string> validateRfBudget(const RfBudget &budget);

// Format-1 project JSON; an invalid budget writes the invalid marker instead.
nlohmann::json rfBudgetToJson(const RfBudget &budget);
// Never throws on shape: a malformed object decodes to `invalid_reason` only. A well-formed
// table is returned intact even when `validateRfBudget()` refuses it, so a budget built in
// steps survives a reload; callers that require completeness validate explicitly.
RfBudget rfBudgetFromJson(const nlohmann::json &value);

const RfBudgetModeRecord *findRfBudgetModeRecord(const RfBudget &budget, std::uint32_t mode_id);
RfBudgetModeRecord *findRfBudgetModeRecord(RfBudget &budget, std::uint32_t mode_id);

// Table editing helpers. They keep references consistent: a stage rename rewrites every feed,
// output, and mode override key; a removal drops the references to the removed stage. A refusal
// leaves the budget unchanged and writes one sentence to `error`.
bool addRfBudgetStage(RfBudget &budget, std::string_view type, std::string &error);
bool renameRfBudgetStage(RfBudget &budget, std::size_t index, std::string_view name,
                         std::string &error);
bool removeRfBudgetStage(RfBudget &budget, std::size_t index);
// Keeps the input feeds that still exist, drops feeds and outputs that name a vanished output
// port, erases the stage's per-mode overrides, and clears params/part_number.
void setRfBudgetStageType(RfBudget &budget, std::size_t index, std::string_view type);
bool addRfBudgetOutput(RfBudget &budget, std::size_t stage_index, int port, std::string &error);
bool renameRfBudgetOutput(RfBudget &budget, std::size_t index, std::string_view name,
                          std::string &error);
bool removeRfBudgetOutput(RfBudget &budget, std::size_t index);
RfBudgetModeRecord &ensureRfBudgetModeRecord(RfBudget &budget, std::uint32_t mode_id);
void eraseRfBudgetModeRecord(RfBudget &budget, std::uint32_t mode_id);
void setRfBudgetOverride(RfBudget &budget, std::uint32_t mode_id, const std::string &stage,
                         const std::string &key, double value);
bool eraseRfBudgetOverride(RfBudget &budget, std::uint32_t mode_id, const std::string &stage,
                           const std::string &key);
