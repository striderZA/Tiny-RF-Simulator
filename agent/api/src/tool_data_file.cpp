#include "agent_args.h"
#include "agent_library.h"
#include "agent_tools.h"
#include "component_library.h"
#include "touchstone_parser.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace {

using Json = nlohmann::json;

constexpr double kDegreesPerRadian = 180.0 / 3.14159265358979323846;

[[noreturn]] void fail(AgentError error) { throw AgentArgumentError(std::move(error)); }

[[noreturn]] void invalid(std::string path, std::string message,
                          std::optional<std::string> hint = std::nullopt) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message), std::move(hint)};
    error.details = {{"path", std::move(path)}};
    fail(std::move(error));
}

[[noreturn]] void notFound(std::string message) {
    fail(AgentError{AgentErrorCode::NotFound, std::move(message)});
}

const char *formatName(TouchstoneData::Format format) {
    switch (format) {
    case TouchstoneData::Format::DB:
        return "DB";
    case TouchstoneData::Format::RI:
        return "RI";
    case TouchstoneData::Format::MA:
        break;
    }
    return "MA";
}

} // namespace

AgentToolResult executeDataFileReadTool(const AgentApi &api, const AgentCall &call) {
    AgentArgs args(call.arguments,
                   {"epoch", "component", "part_number", "type", "s_row", "s_col", "max_points"});
    const bool by_component = call.arguments.contains("component");
    const bool by_part_number = call.arguments.contains("part_number");
    if (by_component == by_part_number)
        invalid("/", by_component ? "give component or part_number, not both"
                                  : "give component or part_number");
    const int row = args.optionalInt("s_row", 1, 0, 3);
    const int col = args.optionalInt("s_col", 0, 0, 3);
    const int max_points = args.optionalInt("max_points", 201, 2, 401);

    const std::uint64_t epoch = api.epoch();
    std::filesystem::path file;
    std::string type;
    std::optional<std::string> part_number;
    if (by_component) {
        const std::uint64_t sent_epoch = args.requiredUInt64("epoch");
        if (sent_epoch != epoch)
            return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                              api.m_undone_summaries);
        const int id = args.requiredInt("component");
        IComponentEngine *component = findById(api.m_context.runtime, id);
        if (!component)
            notFound("component " + std::to_string(id) +
                     " is not in the circuit; call circuit_get");
        const Json serialized = component->serialize();
        const auto stored = serialized.find("sparam_filepath");
        if (stored == serialized.end() || !stored->is_string() ||
            stored->get<std::string>().empty())
            notFound("component has no S-parameter file");
        const std::filesystem::path stored_path(stored->get<std::string>());
        if (!stored_path.is_absolute())
            notFound("stored data-file path is not absolute");
        file = stored_path;
        type = std::string(component->type_name());
    } else {
        // The library route does not check epoch, but a supplied epoch must still be an integer.
        if (call.arguments.contains("epoch"))
            static_cast<void>(args.requiredUInt64("epoch"));
        const std::string number = args.optionalString("part_number", std::string{});
        const std::string wanted_type = args.optionalString("type", std::string{});
        const bool type_supplied = call.arguments.contains("type");
        const ComponentLibrary &library = api.m_context.library;
        const auto parts = matchingParts(library, number, wanted_type, type_supplied);
        if (parts.empty()) {
            AgentError error{AgentErrorCode::UnknownPart, "unknown library part " + number};
            error.details = {{"candidates", partCandidates(library, number)}};
            fail(std::move(error));
        }
        if (parts.size() > 1) {
            AgentError error{AgentErrorCode::AmbiguousPart,
                             "library part " + number + " matches several definitions; give type"};
            error.details = {{"candidates", partCandidates(library, number)}};
            fail(std::move(error));
        }
        const ComponentDefinition &definition = parts.front();
        const bool has_s_entry =
            std::any_of(definition.data_files.begin(), definition.data_files.end(),
                        [](const DataFileRef &ref) { return ref.type == "s_parameters"; });
        if (!has_s_entry)
            notFound("library part " + number + " has no S-parameter data file");
        const auto resolved = componentDataFilePath(definition, "s_parameters");
        if (!resolved)
            notFound("data file path escapes the library directory");
        file = *resolved;
        type = definition.type;
        part_number = definition.part_number;
    }

    std::error_code error_code;
    if (!std::filesystem::is_regular_file(file, error_code))
        notFound("data file is missing on disk");
    const auto parsed = TouchstoneParser::parse(file.string());
    if (!parsed || parsed->frequencies.empty())
        return agentErrorResult(
            AgentError{AgentErrorCode::Internal, "data file could not be parsed"}, epoch);
    if (parsed->parameter != TouchstoneData::Parameter::S)
        notFound("file is not S-parameter data");
    const int ports = parsed->num_ports;
    const std::string port_hint = "file has " + std::to_string(ports) + " ports";
    if (row >= ports)
        invalid("/s_row", "s_row is outside the file's port matrix", port_hint);
    if (col >= ports)
        invalid("/s_col", "s_col is outside the file's port matrix", port_hint);

    const std::size_t points = parsed->frequencies.size();
    const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(max_points), points);
    const std::size_t element = static_cast<std::size_t>(row * ports + col);
    Json samples = Json::array();
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t index =
            count == 1 ? 0
                       : static_cast<std::size_t>(
                             std::round(static_cast<double>(k) * static_cast<double>(points - 1) /
                                        static_cast<double>(count - 1)));
        const std::complex<double> value = parsed->parameters.at(index).at(element);
        samples.push_back(Json{
            {"freq_Hz", parsed->frequencies[index]},
            {"magnitude_dB", agentNumber(20.0 * std::log10(std::abs(value)))},
            {"phase_deg", agentNumber(std::atan2(value.imag(), value.real()) * kDegreesPerRadian)},
        });
    }

    AgentToolResult result;
    result.structured = Json{
        {"epoch", epoch},
        {"source", by_component ? "component" : "library"},
        {"type", type},
        {"part_number", part_number ? Json(*part_number) : Json(nullptr)},
        {"file", file.filename().string()},
        {"ports", ports},
        {"points", points},
        {"reference_impedance_ohm", parsed->reference_impedance},
        {"format", formatName(parsed->format)},
        {"frequency_range_Hz",
         Json{{"min", parsed->frequencies.front()}, {"max", parsed->frequencies.back()}}},
        {"s_parameter", Json{{"row", row}, {"col", col}}},
        {"samples", std::move(samples)},
    };
    return result;
}
