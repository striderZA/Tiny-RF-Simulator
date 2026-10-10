#include "agent_catalog.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;

Json properties(std::initializer_list<std::pair<std::string_view, Json>> fields) {
    Json result = Json::object();
    for (const auto &[name, schema] : fields) {
        result[std::string(name)] = schema;
    }
    return result;
}

Json stringSchema(std::string_view description = {}) {
    Json result{{"type", "string"}};
    if (!description.empty()) {
        result["description"] = std::string(description);
    }
    return result;
}

Json integerSchema(std::string_view description = {}, std::optional<int> minimum = std::nullopt,
                   std::optional<int> maximum = std::nullopt) {
    Json result{{"type", "integer"}};
    if (!description.empty()) {
        result["description"] = std::string(description);
    }
    if (minimum.has_value()) {
        result["minimum"] = *minimum;
    }
    if (maximum.has_value()) {
        result["maximum"] = *maximum;
    }
    return result;
}

Json numberSchema(std::string_view description = {}, std::optional<double> minimum = std::nullopt,
                  std::optional<double> maximum = std::nullopt) {
    Json result{{"type", "number"}};
    if (!description.empty()) {
        result["description"] = std::string(description);
    }
    if (minimum.has_value()) {
        result["minimum"] = *minimum;
    }
    if (maximum.has_value()) {
        result["maximum"] = *maximum;
    }
    return result;
}

Json booleanSchema(std::string_view description = {}) {
    Json result{{"type", "boolean"}};
    if (!description.empty()) {
        result["description"] = std::string(description);
    }
    return result;
}

Json withDefault(Json schema, Json value) {
    schema["default"] = std::move(value);
    return schema;
}

Json objectSchema(Json fields, std::initializer_list<std::string_view> required = {}) {
    Json result{
        {"type", "object"}, {"properties", std::move(fields)}, {"additionalProperties", false}};
    if (required.size() != 0) {
        result["required"] = Json::array();
        for (const auto name : required) {
            result["required"].push_back(std::string(name));
        }
    }
    return result;
}

Json inputSchema(Json fields, std::initializer_list<std::string_view> required, Json example) {
    Json result = objectSchema(std::move(fields), required);
    if (!result.contains("required")) {
        result["required"] = Json::array();
    }
    result["examples"] = Json::array({std::move(example)});
    return result;
}

Json arraySchema(Json item, std::optional<int> minimum = std::nullopt,
                 std::optional<int> maximum = std::nullopt) {
    Json result{{"type", "array"}, {"items", std::move(item)}};
    if (minimum.has_value()) {
        result["minItems"] = *minimum;
    }
    if (maximum.has_value()) {
        result["maxItems"] = *maximum;
    }
    return result;
}

Json nullableSchema(Json non_null) {
    return Json{{"anyOf", Json::array({std::move(non_null), Json{{"type", "null"}}})}};
}

Json enumSchema(std::initializer_list<std::string_view> values) {
    Json result{{"type", "string"}, {"enum", Json::array()}};
    for (const auto value : values) {
        result["enum"].push_back(std::string(value));
    }
    return result;
}

Json constString(std::string_view value) {
    return Json{{"type", "string"}, {"const", std::string(value)}};
}

Json paramsSchema() { return Json{{"type", "object"}, {"additionalProperties", true}}; }

Json portLabelSchema() {
    return objectSchema(properties({{"port", integerSchema("Zero-based port index.", 0)},
                                    {"label", stringSchema("Human-readable port label.")}}),
                        {"port", "label"});
}

Json componentPortSchema() {
    return objectSchema(properties({{"component", integerSchema("Component id.", 0)},
                                    {"port", integerSchema("Zero-based port index.", 0)}}),
                        {"component", "port"});
}

Json refPortSchema() {
    return objectSchema(
        properties({{"ref", Json{{"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_]{0,31}$"}}},
                    {"port", integerSchema("Zero-based port index.", 0)}}),
        {"ref", "port"});
}

Json componentOrRefPortSchema() {
    return Json{{"oneOf", Json::array({componentPortSchema(), refPortSchema()})}};
}

Json positionSchema() {
    return objectSchema(properties({{"x", numberSchema("ImNodes editor-space x coordinate.")},
                                    {"y", numberSchema("ImNodes editor-space y coordinate.")}}),
                        {"x", "y"});
}

Json libraryPartInputSchema() {
    return objectSchema(properties({{"part_number", stringSchema("Exact library part number.")},
                                    {"type", stringSchema("Optional component type filter.")}}),
                        {"part_number"});
}

Json addOperationSchema(bool use_library_part) {
    Json fields =
        properties({{"op", constString("add")},
                    {"ref", Json{{"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_]{0,31}$"}}},
                    {"params", paramsSchema()},
                    {"position", positionSchema()}});
    if (use_library_part) {
        fields["library_part"] = libraryPartInputSchema();
        return objectSchema(std::move(fields), {"op", "library_part"});
    }
    fields["type"] = stringSchema("Built-in component type.");
    return objectSchema(std::move(fields), {"op", "type"});
}

Json removeOperationSchema() {
    return objectSchema(properties({{"op", constString("remove")},
                                    {"component", integerSchema("Component id to remove.", 0)}}),
                        {"op", "component"});
}

Json setParamsOperationSchema(bool use_ref) {
    Json fields = properties({{"op", constString("set_params")}, {"params", paramsSchema()}});
    if (use_ref) {
        fields["ref"] = Json{{"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_]{0,31}$"}};
        return objectSchema(std::move(fields), {"op", "ref", "params"});
    }
    fields["component"] = integerSchema("Component id to update.", 0);
    return objectSchema(std::move(fields), {"op", "component", "params"});
}

Json connectOperationSchema() {
    return objectSchema(properties({{"op", constString("connect")},
                                    {"from", componentOrRefPortSchema()},
                                    {"to", componentOrRefPortSchema()}}),
                        {"op", "from", "to"});
}

Json disconnectOperationSchema() {
    return objectSchema(
        properties({{"op", constString("disconnect")}, {"to", componentOrRefPortSchema()}}),
        {"op", "to"});
}

Json probeOperationSchema(std::string_view operation) {
    return objectSchema(
        properties({{"op", constString(operation)}, {"at", componentOrRefPortSchema()}}),
        {"op", "at"});
}

Json editOperationSchema() {
    return Json{
        {"oneOf",
         Json::array({addOperationSchema(false), addOperationSchema(true), removeOperationSchema(),
                      setParamsOperationSchema(false), setParamsOperationSchema(true),
                      connectOperationSchema(), disconnectOperationSchema(),
                      probeOperationSchema("probe_add"), probeOperationSchema("probe_remove")})}};
}

Json outputEndpointSchema() { return componentPortSchema(); }

Json sourceLinkSchema() { return nullableSchema(componentPortSchema()); }

Json componentInputSchema() {
    return objectSchema(properties({{"port", integerSchema("Zero-based input port.", 0)},
                                    {"label", stringSchema()},
                                    {"source", sourceLinkSchema()}}),
                        {"port", "label", "source"});
}

Json componentOutputSchema() {
    return objectSchema(properties({{"port", integerSchema("Zero-based output port.", 0)},
                                    {"label", stringSchema()},
                                    {"destinations", arraySchema(componentPortSchema())}}),
                        {"port", "label", "destinations"});
}

Json parameterInfoSchema() {
    return objectSchema(properties({{"path", stringSchema()},
                                    {"kind", stringSchema()},
                                    {"unit", stringSchema()},
                                    {"enum_labels", arraySchema(stringSchema())},
                                    {"help", stringSchema()},
                                    {"read_only", booleanSchema()}}),
                        {"path", "kind", "unit", "help", "read_only"});
}

Json typeDescriptorSchema() {
    return objectSchema(properties({{"type", stringSchema()},
                                    {"display_name", stringSchema()},
                                    {"label_prefix", stringSchema()},
                                    {"inputs", arraySchema(portLabelSchema())},
                                    {"outputs", arraySchema(portLabelSchema())},
                                    {"default_params", paramsSchema()},
                                    {"param_info", arraySchema(parameterInfoSchema())}}),
                        {"type", "display_name", "label_prefix", "inputs", "outputs"});
}

Json circuitComponentSchema(bool include_params) {
    Json fields = properties({{"id", integerSchema("Component id.", 0)},
                              {"type", stringSchema()},
                              {"label", stringSchema()},
                              {"part_number", stringSchema()},
                              {"inputs", arraySchema(componentInputSchema())},
                              {"outputs", arraySchema(componentOutputSchema())}});
    if (include_params) {
        fields["params"] = paramsSchema();
    }
    return objectSchema(std::move(fields), {"id", "type", "label", "inputs", "outputs"});
}

Json networkAnalyzerPointsSchema() {
    return objectSchema(properties({{"point_a", nullableSchema(outputEndpointSchema())},
                                    {"point_b", nullableSchema(outputEndpointSchema())},
                                    {"start_Hz", nullableSchema(numberSchema())},
                                    {"stop_Hz", nullableSchema(numberSchema())},
                                    {"points", integerSchema("Sweep point count.")},
                                    {"stimulus_dBm", nullableSchema(numberSchema())}}),
                        {"point_a", "point_b", "start_Hz", "stop_Hz", "points", "stimulus_dBm"});
}

Json parameterChangeSchema() {
    const Json value_schema{{"type", Json::array({"object", "array", "string", "number", "integer",
                                                  "boolean", "null"})}};
    return objectSchema(
        properties(
            {{"path", stringSchema()}, {"old_value", value_schema}, {"new_value", value_schema}}),
        {"path", "old_value", "new_value"});
}

Json appliedOperationSchema() {
    return objectSchema(properties({{"op_index", integerSchema("Zero-based operation index.", 0)},
                                    {"op", enumSchema({"add", "remove", "set_params", "connect",
                                                       "disconnect", "probe_add", "probe_remove"})},
                                    {"component", integerSchema("Affected component id.", 0)},
                                    {"ref", stringSchema()},
                                    {"label", stringSchema()},
                                    {"params", paramsSchema()},
                                    {"also_changed", arraySchema(parameterChangeSchema())},
                                    {"unchanged", booleanSchema()}}),
                        {"op_index", "op"});
}

Json circuitEditSuccessSchema() {
    Json refs{{"type", "object"}, {"additionalProperties", integerSchema("Component id.", 0)}};
    return objectSchema(
        properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                    {"revision", integerSchema("Project revision after the call.", 0)},
                    {"applied", arraySchema(appliedOperationSchema())},
                    {"refs", std::move(refs)}}),
        {"epoch", "revision", "applied", "refs"});
}

Json errorDetailValueSchema() {
    return Json{{"type", Json::array({"object", "array", "string", "number", "integer", "boolean",
                                      "null"})}};
}

Json traceInputSchema() {
    return objectSchema(
        properties(
            {{"start_Hz", numberSchema("First trace frequency.")},
             {"stop_Hz", numberSchema("Last trace frequency.")},
             {"points", withDefault(integerSchema("Number of trace intervals.", 2, 401), 201)}}),
        {"start_Hz", "stop_Hz"});
}

Json measurementSnrBasisSchema() {
    Json rbw = objectSchema(
        properties({{"kind", constString("rbw")}, {"rbw_Hz", nullableSchema(numberSchema())}}),
        {"kind", "rbw_Hz"});
    Json pfb = objectSchema(properties({{"kind", constString("pfb_channel")},
                                        {"enbw_Hz", nullableSchema(numberSchema())},
                                        {"channel_noise_dBm", nullableSchema(numberSchema())}}),
                            {"kind", "enbw_Hz", "channel_noise_dBm"});
    return Json{{"oneOf", Json::array({std::move(rbw), std::move(pfb)})}};
}

Json measurementTraceSchema() {
    return objectSchema(
        properties({{"frequencies_Hz", arraySchema(nullableSchema(numberSchema()))},
                    {"noise_dBm_per_Hz", arraySchema(nullableSchema(numberSchema()))}}),
        {"frequencies_Hz", "noise_dBm_per_Hz"});
}

Json networkMetricSummarySchema() {
    return objectSchema(properties({{"min", nullableSchema(numberSchema())},
                                    {"max", nullableSchema(numberSchema())},
                                    {"mean", nullableSchema(numberSchema())},
                                    {"at_start", nullableSchema(numberSchema())},
                                    {"at_center", nullableSchema(numberSchema())},
                                    {"at_stop", nullableSchema(numberSchema())}}),
                        {"min", "max", "mean", "at_start", "at_center", "at_stop"});
}

Json networkAnalyzerSuccessSchema() {
    Json arrays =
        objectSchema(properties({{"frequencies_Hz", arraySchema(nullableSchema(numberSchema()))},
                                 {"gain_dB", arraySchema(nullableSchema(numberSchema()))},
                                 {"nf_dB", arraySchema(nullableSchema(numberSchema()))}}),
                     {"frequencies_Hz", "gain_dB", "nf_dB"});
    Json summary =
        objectSchema(properties({{"valid_points", integerSchema("Valid sweep points.", 0)},
                                 {"gain_dB", networkMetricSummarySchema()},
                                 {"nf_dB", networkMetricSummarySchema()}}),
                     {"valid_points", "gain_dB", "nf_dB"});
    return objectSchema(properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                                    {"settings", networkAnalyzerPointsSchema()},
                                    {"summary", std::move(summary)},
                                    {"arrays", std::move(arrays)}}),
                        {"epoch", "settings", "summary"});
}

Json dataFileReadSuccessSchema() {
    Json range = objectSchema(properties({{"min", numberSchema("Lowest frequency in Hz.")},
                                          {"max", numberSchema("Highest frequency in Hz.")}}),
                              {"min", "max"});
    Json selector = objectSchema(properties({{"row", integerSchema("S-parameter row.", 0, 3)},
                                             {"col", integerSchema("S-parameter column.", 0, 3)}}),
                                 {"row", "col"});
    Json sample = objectSchema(properties({{"freq_Hz", numberSchema("Sample frequency in Hz.")},
                                           {"magnitude_dB", nullableSchema(numberSchema())},
                                           {"phase_deg", nullableSchema(numberSchema())}}),
                               {"freq_Hz", "magnitude_dB", "phase_deg"});
    return objectSchema(
        properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                    {"source", enumSchema({"component", "library"})},
                    {"type", stringSchema("Component type of the source.")},
                    {"part_number", nullableSchema(stringSchema())},
                    {"file", stringSchema("Data file basename.")},
                    {"ports", integerSchema("Port count of the file.", 1)},
                    {"points", integerSchema("Frequency point count of the file.", 1)},
                    {"reference_impedance_ohm", numberSchema("Reference impedance in ohms.")},
                    {"format", enumSchema({"DB", "MA", "RI"})},
                    {"frequency_range_Hz", std::move(range)},
                    {"s_parameter", std::move(selector)},
                    {"samples", arraySchema(std::move(sample), 1, 401)}}),
        {"epoch", "source", "type", "part_number", "file", "ports", "points",
         "reference_impedance_ohm", "format", "frequency_range_Hz", "s_parameter", "samples"});
}

Json testFlowRunSuccessSchema() {
    Json condition =
        objectSchema(properties({{"component", integerSchema("Swept component id.", 0)},
                                 {"path", stringSchema("Swept parameter path.")},
                                 {"value", numberSchema("Swept value for this row.")}}),
                     {"component", "path", "value"});
    Json metric = objectSchema(
        properties({{"component", integerSchema("Measured component id.", 0)},
                    {"port", integerSchema("Output port index.", 0)},
                    {"name", stringSchema("Metric name.")},
                    {"value", nullableSchema(numberSchema("Metric value; null when not finite."))},
                    {"unit", stringSchema("Unit of the value.")},
                    {"valid", booleanSchema("False when the metric has no reading.")}}),
        {"component", "port", "name", "value", "unit", "valid"});
    Json row = objectSchema(properties({{"conditions", arraySchema(std::move(condition), 0, 4)},
                                        {"metrics", arraySchema(std::move(metric), 1, 8)}}),
                            {"conditions", "metrics"});
    return objectSchema(properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                                    {"row_count", integerSchema("Rows in the sweep.", 1, 1000)},
                                    {"rows", arraySchema(std::move(row), 1, 1000)}}),
                        {"epoch", "row_count", "rows"});
}

Json outputSchema(Json success) {
    return Json{{"type", "object"},
                {"anyOf", Json::array({std::move(success), agentErrorSchema()})}};
}

AgentToolDefinition tool(std::string name, std::string title, std::string description, Json input,
                         Json success, Json annotations) {
    return {std::move(name),
            std::move(title),
            std::move(description),
            std::move(input),
            outputSchema(std::move(success)),
            std::move(annotations)};
}

Json readOnlyAnnotations() { return Json{{"readOnlyHint", true}, {"openWorldHint", false}}; }

Json destructiveAnnotations() { return Json{{"destructiveHint", true}, {"openWorldHint", false}}; }

Json idempotentAnnotations() { return Json{{"idempotentHint", true}, {"openWorldHint", false}}; }

} // namespace

nlohmann::json agentErrorSchema() {
    Json error = objectSchema(
        properties(
            {{"code",
              enumSchema({"INVALID_ARGUMENT", "NOT_FOUND", "STALE_EPOCH", "UNKNOWN_TYPE",
                          "UNKNOWN_PART", "AMBIGUOUS_PART", "PARAM_REJECTED",
                          "PATH_PARAMS_UNSUPPORTED", "LINK_REJECTED", "NO_MEASUREMENT", "BUSY",
                          "SIMULATOR_UNAVAILABLE", "VERSION_MISMATCH", "INTERNAL"})},
             {"message", stringSchema("Problem description and recommended next step.")},
             {"hint", stringSchema("Optional recovery guidance.")},
             {"op_index", integerSchema("Zero-based failing operation index.", 0)},
             {"details",
              objectSchema(properties(
                  {{"path", stringSchema("Invalid argument path.")},
                   {"epoch", integerSchema("Current circuit epoch.", 0)},
                   {"cause", enumSchema({"new_project", "opened_project", "tutorial", "reverted"})},
                   {"undone", arraySchema(stringSchema())},
                   {"suggestions", arraySchema(stringSchema(), std::nullopt, 5)},
                   {"expected", stringSchema()},
                   {"existing_source", componentPortSchema()},
                   {"candidates", arraySchema(errorDetailValueSchema())},
                   {"reason",
                    enumSchema({"UNKNOWN_KEY", "TYPE_MISMATCH", "READ_ONLY", "ENGINE_ADJUSTED",
                                "DESERIALIZE_FAILED", "RESTORE_FAILED", "INPUT_OCCUPIED", "CYCLE",
                                "ADC_TO_PFB_ONLY", "POLICY", "NO_PATH", "NO_VALID_POINTS"})},
                   {"requested", errorDetailValueSchema()},
                   {"stored", errorDetailValueSchema()},
                   {"limit", integerSchema("Maximum allowed value.", 0)}}))}}),
        {"code", "message"});
    Json refs{{"type", "object"}, {"additionalProperties", integerSchema("Component id.", 0)}};
    return objectSchema(properties({{"epoch", nullableSchema(integerSchema("Circuit epoch.", 0))},
                                    {"error", std::move(error)},
                                    {"applied", arraySchema(appliedOperationSchema())},
                                    {"refs", std::move(refs)}}),
                        {"epoch", "error"});
}

const std::vector<AgentToolDefinition> &agentToolCatalog() {
    static const std::vector<AgentToolDefinition> catalog{
        tool(
            "component_types", "Component Types",
            "List registered component types and their ports; for a selected type, include its "
            "default parameters and parameter metadata.",
            inputSchema(properties({{"type", stringSchema("Optional component type to inspect.")}}),
                        {}, Json::object()),
            objectSchema(properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                                     {"types", arraySchema(typeDescriptorSchema())}}),
                         {"epoch", "types"}),
            readOnlyAnnotations()),
        tool(
            "library_search", "Library Search",
            "Search loaded component-library definitions by part number, manufacturer, and "
            "description.",
            inputSchema(
                properties({{"query", stringSchema("Case-insensitive substring query.")},
                            {"type", stringSchema("Optional component type filter.")},
                            {"limit",
                             withDefault(integerSchema("Maximum number of matches.", 1, 50), 20)}}),
                {}, Json{{"query", "LNA"}, {"type", "amplifier"}, {"limit", 10}}),
            objectSchema(
                properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                            {"parts", arraySchema(objectSchema(
                                          properties({{"part_number", stringSchema()},
                                                      {"type", stringSchema()},
                                                      {"manufacturer", stringSchema()},
                                                      {"description", stringSchema()},
                                                      {"parameters", paramsSchema()},
                                                      {"has_data_files", booleanSchema()}}),
                                          {"part_number", "type", "manufacturer", "description",
                                           "parameters", "has_data_files"}))},
                            {"total", integerSchema("Total matching loaded definitions.", 0)}}),
                {"epoch", "parts", "total"}),
            readOnlyAnnotations()),
        tool("circuit_get", "Get Circuit",
             "Read the current project circuit, component ids, topology, probes, and "
             "network-analyzer settings.",
             inputSchema(
                 properties({{"include_params", Json{{"type", "boolean"}, {"default", false}}}}),
                 {}, Json{{"include_params", true}}),
             objectSchema(
                 properties({{"epoch", integerSchema("Circuit epoch.", 0)},
                             {"revision", integerSchema("Project revision.", 0)},
                             {"dirty", booleanSchema()},
                             {"project_name", nullableSchema(stringSchema(
                                                  "Project file stem, or null when untitled."))},
                             {"components", arraySchema(circuitComponentSchema(true))},
                             {"probes", arraySchema(componentPortSchema())},
                             {"network_analyzer", networkAnalyzerPointsSchema()}}),
                 {"epoch", "revision", "dirty", "project_name", "components", "probes",
                  "network_analyzer"}),
             readOnlyAnnotations()),
        tool("component_get", "Get Component",
             "Read one component's parameters, ports, parameter metadata, and hover summary using "
             "its current id and epoch.",
             inputSchema(
                 properties({{"epoch", integerSchema("Epoch from the latest circuit read.", 0)},
                             {"component", integerSchema("Component id.", 0)}}),
                 {"epoch", "component"}, Json{{"epoch", 3}, {"component", 103}}),
             objectSchema(
                 properties({{"epoch", integerSchema("Current circuit epoch.", 0)},
                             {"id", integerSchema("Component id.", 0)},
                             {"type", stringSchema()},
                             {"label", stringSchema()},
                             {"part_number", stringSchema()},
                             {"params", paramsSchema()},
                             {"param_info", arraySchema(parameterInfoSchema())},
                             {"inputs", arraySchema(componentInputSchema())},
                             {"outputs", arraySchema(componentOutputSchema())},
                             {"summary", stringSchema("ComponentRegistry hover-summary text.")}}),
                 {"epoch", "id", "type", "label", "params", "param_info", "inputs", "outputs",
                  "summary"}),
             readOnlyAnnotations()),
        tool("circuit_edit", "Edit Circuit",
             "Apply an ordered list of circuit operations using the epoch from the latest circuit "
             "read.",
             inputSchema(
                 properties({{"epoch", integerSchema("Epoch from the latest circuit read.", 0)},
                             {"ops", arraySchema(editOperationSchema(), 1, 64)}}),
                 {"epoch", "ops"},
                 Json{{"epoch", 3},
                      {"ops",
                       Json::array({Json{{"op", "add"}, {"type", "amplifier"}, {"ref", "amp"}}})}}),
             circuitEditSuccessSchema(), destructiveAnnotations()),
        tool(
            "measure_port", "Measure Port",
            "Measure an output port's power, tones, noise, SNR, and optional trace. For "
            "real-domain signals, the reported single-sided tone power is 3.01 dB above the "
            "spectrum analyzer's displayed peak.",
            inputSchema(
                properties(
                    {{"epoch", integerSchema("Epoch from the latest circuit read.", 0)},
                     {"at", outputEndpointSchema()},
                     {"max_tones", withDefault(integerSchema("Maximum returned tones.", 1, 32), 8)},
                     {"trace", traceInputSchema()}}),
                {"epoch", "at"},
                Json{
                    {"epoch", 3}, {"at", Json{{"component", 103}, {"port", 0}}}, {"max_tones", 8}}),
            objectSchema(
                properties(
                    {{"epoch", integerSchema("Current circuit epoch.", 0)},
                     {"total_power_dBm", nullableSchema(numberSchema())},
                     {"peak",
                      objectSchema(properties({{"freq_Hz", nullableSchema(numberSchema())},
                                               {"power_dBm", nullableSchema(numberSchema())}}),
                                   {"freq_Hz", "power_dBm"})},
                     {"noise_floor_dBm_per_Hz", nullableSchema(numberSchema())},
                     {"tones", arraySchema(objectSchema(
                                   properties({{"freq_Hz", nullableSchema(numberSchema())},
                                               {"power_dBm", nullableSchema(numberSchema())},
                                               {"phase_deg", nullableSchema(numberSchema())}}),
                                   {"freq_Hz", "power_dBm", "phase_deg"}))},
                     {"tone_count", integerSchema("Number of stored tones.", 0)},
                     {"snr_dB", nullableSchema(numberSchema())},
                     {"snr_basis", measurementSnrBasisSchema()},
                     {"fs_Hz", nullableSchema(numberSchema())},
                     {"is_complex_baseband", booleanSchema()},
                     {"trace", measurementTraceSchema()}}),
                {"epoch", "total_power_dBm", "peak", "noise_floor_dBm_per_Hz", "tones",
                 "tone_count", "snr_dB", "snr_basis", "fs_Hz", "is_complex_baseband"}),
            readOnlyAnnotations()),
        tool("network_analyzer_sweep", "Network Analyzer Sweep",
             "Set network-analyzer sweep settings at two output ports and return gain/noise-figure "
             "summary data, with optional sampled arrays.",
             inputSchema(
                 properties(
                     {{"epoch", integerSchema("Epoch from the latest circuit read.", 0)},
                      {"point_a", outputEndpointSchema()},
                      {"point_b", outputEndpointSchema()},
                      {"start_Hz", numberSchema("Sweep start frequency.")},
                      {"stop_Hz", numberSchema("Sweep stop frequency.")},
                      {"points", integerSchema("Sweep point count.")},
                      {"stimulus_dBm", numberSchema("Analyzer stimulus power.")},
                      {"arrays",
                       objectSchema(
                           properties({{"max_points",
                                        integerSchema("Maximum sampled array points.", 2, 401)}}),
                           {"max_points"})}}),
                 {"epoch", "point_a", "point_b"},
                 Json{{"epoch", 3},
                      {"point_a", Json{{"component", 103}, {"port", 0}}},
                      {"point_b", Json{{"component", 104}, {"port", 0}}},
                      {"start_Hz", 1.0e6},
                      {"stop_Hz", 1.0e9},
                      {"points", 201},
                      {"arrays", Json{{"max_points", 101}}}}),
             networkAnalyzerSuccessSchema(), idempotentAnnotations()),
        tool("data_file_read", "Read Data File",
             "Summarize the S-parameter file behind a circuit component or library part. Takes "
             "no file paths and never writes.",
             inputSchema(
                 properties(
                     {{"epoch", integerSchema("Epoch from the latest circuit read; required with "
                                              "component.",
                                              0)},
                      {"component",
                       integerSchema("Component id whose S-parameter file to read.", 0)},
                      {"part_number", stringSchema("Exact library part number.")},
                      {"type",
                       stringSchema("Optional component type that disambiguates part_number.")},
                      {"s_row", withDefault(integerSchema("S-parameter row, 0 to 3.", 0, 3), 1)},
                      {"s_col", withDefault(integerSchema("S-parameter column, 0 to 3.", 0, 3), 0)},
                      {"max_points",
                       withDefault(integerSchema("Maximum number of samples, 2 to 401.", 2, 401),
                                   201)}}),
                 {}, Json{{"part_number", "AM1143"}}),
             dataFileReadSuccessSchema(), readOnlyAnnotations()),
        tool("test_flow_run", "Run Test Flow",
             "Run a parametric sweep of component parameters in one call. Each row sets the "
             "swept values, measures the output ports, and restores the circuit, so the project "
             "is left unchanged. Returns at most 1000 rows.",
             inputSchema(
                 properties(
                     {{"epoch", integerSchema("Epoch from the latest circuit read.", 0)},
                      {"conditions",
                       arraySchema(
                           objectSchema(
                               properties(
                                   {{"component", integerSchema("Swept component id.", 0)},
                                    {"path",
                                     stringSchema("Parameter path, e.g. tones[0].power_dBm.")},
                                    {"values", arraySchema(numberSchema("Swept value."), 1)}}),
                               {"component", "path", "values"}),
                           0, 4)},
                      {"measure",
                       arraySchema(
                           objectSchema(
                               properties(
                                   {{"component", integerSchema("Measured component id.", 0)},
                                    {"port", integerSchema("Output port index.", 0)},
                                    {"metric", stringSchema("Metric name, e.g. power_dBm.")}}),
                               {"component", "port", "metric"}),
                           1, 8)}}),
                 {"epoch", "measure"},
                 Json{{"epoch", 0},
                      {"measure", Json::array({Json{
                                      {"component", 1}, {"port", 0}, {"metric", "power_dBm"}}})}}),
             testFlowRunSuccessSchema(), idempotentAnnotations())};
    return catalog;
}

const AgentToolDefinition *findAgentTool(std::string_view name) {
    const auto &catalog = agentToolCatalog();
    for (const auto &definition : catalog) {
        if (std::string_view{definition.name} == name) {
            return &definition;
        }
    }
    return nullptr;
}

const std::string &agentServerInstructions() {
    static const std::string instructions =
        "RF Simulator tools act on the project open in the user's RF Simulator window. Start with "
        "circuit_get. Component ids are the numbers in node labels (\"Amplifier 103\" is 103). "
        "Send the epoch from your latest read with every call that takes component ids; on "
        "STALE_EPOCH, call circuit_get again. Parameter units are encoded in key suffixes (_Hz, "
        "_dB, _dBm, _dBm_per_Hz). This version cannot open, save, or name files.";
    return instructions;
}
