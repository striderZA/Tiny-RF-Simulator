#include "coax_presets.h"
#include "component_engine_base.h"
#include "component_params.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "editor_commands.h"
#include "flow_params.h"
#include "graph_editor_actions.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using ExpectedField = std::pair<FieldKind, std::string>;
using ExpectedTypeFields = std::map<std::string, ExpectedField>;

const std::map<std::string, ExpectedTypeFields> &expectedStateFields() {
    static const std::map<std::string, ExpectedTypeFields> fields = {
        {"amplifier",
         {{"gain_dB", {FieldKind::Number, "dB"}},
          {"nf_dB", {FieldKind::Number, "dB"}},
          {"enable_nonlinear", {FieldKind::Bool, ""}},
          {"oip2_dBm", {FieldKind::Number, "dBm"}},
          {"oip3_dBm", {FieldKind::Number, "dBm"}},
          {"p1db_dBm", {FieldKind::Number, "dBm"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}},
          {"sparam_fwd_idx", {FieldKind::Number, ""}}}},
        {"attenuator",
         {{"atten_dB", {FieldKind::Number, "dB"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}}}},
        {"splitter", {}},
        {"filter",
         {{"filter_type", {FieldKind::Enum, ""}},
          {"fc_low_Hz", {FieldKind::Number, "Hz"}},
          {"fc_high_Hz", {FieldKind::Number, "Hz"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}},
          {"sparam_fwd_idx", {FieldKind::Number, ""}}}},
        {"mixer",
         {{"lo_freq_Hz", {FieldKind::Number, "Hz"}},
          {"conv_gain_dB", {FieldKind::Number, "dB"}},
          {"nf_dB", {FieldKind::Number, "dB"}}}},
        {"equalizer",
         {{"ref_gain_dB", {FieldKind::Number, "dB"}},
          {"ref_freq_Hz", {FieldKind::Number, "Hz"}},
          {"slope_dB_per_decade", {FieldKind::Number, "dB/decade"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}},
          {"sparam_fwd_idx", {FieldKind::Number, ""}}}},
        {"combiner",
         {{"manual_mode", {FieldKind::Bool, ""}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}}}},
        {"rf_switch_spdt",
         {{"active_throw", {FieldKind::Enum, ""}},
          {"insertion_loss_dB", {FieldKind::Number, "dB"}},
          {"isolation_dB", {FieldKind::Number, "dB"}}}},
        {"rf_switch_spdt_2to1",
         {{"active_throw", {FieldKind::Enum, ""}},
          {"insertion_loss_dB", {FieldKind::Number, "dB"}},
          {"isolation_dB", {FieldKind::Number, "dB"}}}},
        {"adc",
         {{"sample_rate_Hz", {FieldKind::Number, "Hz"}},
          {"nsd_dBm_per_Hz", {FieldKind::Number, "dBm/Hz"}},
          {"decimation", {FieldKind::Number, ""}},
          {"nco_fs_fraction", {FieldKind::Number, "×Fs"}}}},
        {"generator",
         {{"tones[].freq_Hz", {FieldKind::Number, "Hz"}},
          {"tones[].power_dBm", {FieldKind::Number, "dBm"}},
          {"tones[].phase_deg", {FieldKind::Number, "deg"}},
          {"fs_Hz", {FieldKind::Number, "Hz"}}}},
        {"coax",
         {{"preset_index", {FieldKind::Enum, ""}},
          {"length_m", {FieldKind::Number, "m"}},
          {"connectors_loss_dB", {FieldKind::Number, "dB"}}}},
        {"pfb",
         {{"channel_count", {FieldKind::Number, ""}},
          {"taps_per_branch", {FieldKind::Number, ""}},
          {"kaiser_beta", {FieldKind::Number, ""}},
          {"sampling_ratio", {FieldKind::Number, ""}},
          {"active_channel", {FieldKind::Number, ""}}}},
    };
    return fields;
}

std::set<std::string> keys(const ExpectedTypeFields &fields) {
    std::set<std::string> result;
    for (const auto &[key, _] : fields)
        result.insert(key);
    return result;
}

void checkEnumIndex(const ParameterField *field, const nlohmann::json &snapshot,
                    const std::string &expected_label) {
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == FieldKind::Enum);
    REQUIRE(snapshot.contains(field->key));
    const int stored_index = snapshot.at(field->key).get<int>();
    REQUIRE(stored_index >= 0);
    REQUIRE(static_cast<size_t>(stored_index) < field->enum_values.size());
    CHECK(field->enum_values[static_cast<size_t>(stored_index)] == expected_label);
}

class FakeParamsEngine final : public ComponentEngineBase {
  public:
    FakeParamsEngine(int id, NodeGraphEngine &graph, nlohmann::json state)
        : ComponentEngineBase(id, graph, "Fake", 1, 1), m_state(std::move(state)) {}

    std::string_view type_name() const override { return "fake_params"; }
    std::string hoverSummary() const override { return "Fake"; }
    void update(double) override {}
    nlohmann::json serialize() const override { return m_state; }
    void deserialize(const nlohmann::json &state) override {
        ++deserialize_calls;
        m_state = state;
        if (deserialize_calls == 1 && relative_adjustment != 0.0 && m_state.contains("gain_dB") &&
            m_state["gain_dB"].is_number()) {
            m_state["gain_dB"] = m_state["gain_dB"].get<double>() * (1.0 + relative_adjustment);
        }
        if (throw_on_calls.contains(deserialize_calls))
            throw std::runtime_error("fake deserialize failure");
    }

    int deserialize_calls = 0;
    double relative_adjustment = 0.0;
    std::set<int> throw_on_calls;

  private:
    nlohmann::json m_state;
};

ParameterField testField(std::string key, FieldKind kind = FieldKind::Number,
                         bool read_only = false) {
    ParameterField field;
    field.key = std::move(key);
    field.kind = kind;
    field.read_only = read_only;
    return field;
}

const ComponentTypeDescriptor &paramsDescriptor(std::string_view type) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(type);
    REQUIRE(descriptor != nullptr);
    return *descriptor;
}

struct ParamsEngineFixture {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components{graph, view};
    int next_id = 1;

    IComponentEngine *create(std::string_view type) {
        const auto *descriptor = ComponentTypeRegistry::instance().find(type);
        if (!descriptor || !descriptor->create)
            return nullptr;
        return descriptor->create(components, graph, next_id++);
    }
};

struct FakeParamsFixture {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components{graph, view};

    FakeParamsEngine &create(int id, nlohmann::json state) {
        return components.add<FakeParamsEngine>(id, graph, std::move(state));
    }
};

struct ParamsCommandFixture {
    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    EditorCommands commands{runtime, actions};
};

} // namespace

TEST_CASE("State metadata describes every scalar engine serialization leaf", "[component_params]") {
    const auto descriptors = ComponentTypeRegistry::instance().all();
    REQUIRE(descriptors.size() == expectedStateFields().size());

    std::set<std::string> actual_types;
    for (const auto *descriptor : descriptors)
        actual_types.insert(descriptor->type);
    std::set<std::string> expected_types;
    for (const auto &[type, _] : expectedStateFields())
        expected_types.insert(type);
    CHECK(actual_types == expected_types);

    int id = 1;
    for (const auto *descriptor : descriptors) {
        CAPTURE(descriptor->type);
        REQUIRE(descriptor->create != nullptr);
        const auto expected_type = expectedStateFields().find(descriptor->type);
        REQUIRE(expected_type != expectedStateFields().end());

        NodeGraphEngine graph;
        ViewManager view;
        ComponentRegistry components(graph, view);
        auto *engine = descriptor->create(components, graph, id++);
        REQUIRE(engine != nullptr);
        if (descriptor->type == "generator") {
            engine->deserialize(
                {{"tones", {{{"freq_Hz", 1e8}, {"power_dBm", -20.0}, {"phase_deg", 0.0}}}}});
        }

        const nlohmann::json snapshot = engine->serialize();
        if (descriptor->type == "generator")
            REQUIRE(snapshot.at("tones").size() == 1);

        std::set<std::string> serialized_keys;
        for (const auto &path : describeConditionPaths(snapshot)) {
            const std::string key = normalizeStatePath(path.path);
            serialized_keys.insert(key);
            const ParameterField *field = findStateField(*descriptor, path.path);
            REQUIRE(field != nullptr);
            CHECK(field->key == key);
        }

        std::set<std::string> metadata_keys;
        for (const auto &field : descriptor->state_fields) {
            CAPTURE(field.key);
            metadata_keys.insert(field.key);
            CHECK(field.kind == expected_type->second.at(field.key).first);
            CHECK(field.unit == expected_type->second.at(field.key).second);
            CHECK_FALSE(field.help.empty());
            CHECK(field.help.back() == '.');
            CHECK((std::isinf(field.min) && field.min < 0.0));
            CHECK((std::isinf(field.max) && field.max > 0.0));
            CHECK(field.read_only == (field.key == "sparam_filepath"));
            CHECK((field.kind == FieldKind::FilePath) == (field.key == "sparam_filepath"));
        }

        CHECK(metadata_keys.size() == descriptor->state_fields.size());
        CHECK(serialized_keys == metadata_keys);
        CHECK(metadata_keys == keys(expected_type->second));
        CHECK(std::includes(metadata_keys.begin(), metadata_keys.end(), serialized_keys.begin(),
                            serialized_keys.end()));
        CHECK(std::includes(serialized_keys.begin(), serialized_keys.end(), metadata_keys.begin(),
                            metadata_keys.end()));
    }
}

TEST_CASE("State metadata enum labels index the engine's stored integers", "[component_params]") {
    CHECK(normalizeStatePath("tones[12].freq_Hz") == "tones[].freq_Hz");

    const auto *filter = ComponentTypeRegistry::instance().find("filter");
    REQUIRE(filter != nullptr);
    const ParameterField *filter_type = findStateField(*filter, "filter_type");
    REQUIRE(filter_type != nullptr);
    CHECK(filter_type->enum_values == std::vector<std::string>{"LPF", "HPF", "BPF", "BSF"});

    const auto *spdt = ComponentTypeRegistry::instance().find("rf_switch_spdt");
    REQUIRE(spdt != nullptr);
    const ParameterField *spdt_throw = findStateField(*spdt, "active_throw");
    REQUIRE(spdt_throw != nullptr);
    CHECK(spdt_throw->enum_values == std::vector<std::string>{"T1", "T2"});

    const auto *spdt_2to1 = ComponentTypeRegistry::instance().find("rf_switch_spdt_2to1");
    REQUIRE(spdt_2to1 != nullptr);
    const ParameterField *spdt_2to1_throw = findStateField(*spdt_2to1, "active_throw");
    REQUIRE(spdt_2to1_throw != nullptr);
    CHECK(spdt_2to1_throw->enum_values == std::vector<std::string>{"T1", "T2"});

    const auto *coax = ComponentTypeRegistry::instance().find("coax");
    REQUIRE(coax != nullptr);
    const ParameterField *preset_index = findStateField(*coax, "preset_index");
    REQUIRE(preset_index != nullptr);
    REQUIRE(preset_index->enum_values.size() == kCoaxCablePresets.size());
    for (size_t index = 0; index < kCoaxCablePresets.size(); ++index)
        CHECK(preset_index->enum_values[index] == kCoaxCablePresets[index].name);

    int id = 1;
    for (const auto &[descriptor, field, expected_label] : std::vector<
             std::tuple<const ComponentTypeDescriptor *, const ParameterField *, std::string>>{
             {filter, filter_type, "LPF"},
             {spdt, spdt_throw, "T1"},
             {spdt_2to1, spdt_2to1_throw, "T1"}}) {
        CAPTURE(descriptor->type);
        NodeGraphEngine graph;
        ViewManager view;
        ComponentRegistry components(graph, view);
        auto *engine = descriptor->create(components, graph, id++);
        REQUIRE(engine != nullptr);
        checkEnumIndex(field, engine->serialize(), expected_label);
    }

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components(graph, view);
    auto *coax_engine = coax->create(components, graph, id);
    REQUIRE(coax_engine != nullptr);
    const nlohmann::json coax_snapshot = coax_engine->serialize();
    const int stored_preset = coax_snapshot.at("preset_index").get<int>();
    REQUIRE(stored_preset >= 0);
    REQUIRE(static_cast<size_t>(stored_preset) < kCoaxCablePresets.size());
    checkEnumIndex(preset_index, coax_snapshot, kCoaxCablePresets[stored_preset].name);
}

TEST_CASE("Scalar writes preserve JSON types and coerce integral numbers", "[component_params]") {
    FakeParamsFixture fixture;
    auto &engine = fixture.create(1001, {{"gain_dB", 1.0},
                                         {"channel_count", 32},
                                         {"enable_nonlinear", false},
                                         {"unsigned_slot", std::uint64_t{5}}});
    const std::vector<ParameterField> fields = {testField("gain_dB"), testField("channel_count"),
                                                testField("enable_nonlinear", FieldKind::Bool),
                                                testField("unsigned_slot")};

    const auto gain = applyComponentParams(engine, fields, {{"gain_dB", 12.0}});
    CHECK(gain.status == ParamWriteStatus::Applied);
    CHECK(engine.serialize().at("gain_dB").get<double>() == 12.0);

    const auto integer = applyComponentParams(engine, fields, {{"channel_count", 64.0}});
    CHECK(integer.status == ParamWriteStatus::Applied);
    CHECK(engine.serialize().at("channel_count").is_number_integer());
    CHECK(engine.serialize().at("channel_count").get<int>() == 64);

    const auto fractional = applyComponentParams(engine, fields, {{"channel_count", 64.5}});
    CHECK(fractional.status == ParamWriteStatus::TypeMismatch);
    CHECK(fractional.path == "channel_count");
    CHECK(fractional.expected == "integer");

    const auto wrong_boolean = applyComponentParams(engine, fields, {{"enable_nonlinear", 1}});
    CHECK(wrong_boolean.status == ParamWriteStatus::TypeMismatch);
    CHECK(wrong_boolean.expected == "boolean");

    const auto negative_unsigned = applyComponentParams(engine, fields, {{"unsigned_slot", -1}});
    CHECK(negative_unsigned.status == ParamWriteStatus::TypeMismatch);
    CHECK(negative_unsigned.expected == "integer");
}

TEST_CASE("Arrays replace whole and validate object element fields", "[component_params]") {
    ParamsEngineFixture fixture;
    auto *engine = fixture.create("generator");
    REQUIRE(engine != nullptr);
    engine->deserialize({{"tones",
                          {{{"freq_Hz", 1e8}, {"power_dBm", -20.0}, {"phase_deg", 0.0}},
                           {{"freq_Hz", 2e8}, {"power_dBm", -30.0}, {"phase_deg", 10.0}}}}});

    const auto replaced = applyComponentParams(
        *engine, paramsDescriptor("generator").state_fields,
        {{"tones", {{{"freq_Hz", 3e8}, {"power_dBm", -10.0}, {"phase_deg", 5.0}}}}});
    CHECK(replaced.status == ParamWriteStatus::Applied);
    CHECK(engine->serialize().at("tones").size() == 1);
    CHECK(engine->serialize().at("tones").at(0).at("freq_Hz") == 3e8);

    const auto bad_field = applyComponentParams(*engine, paramsDescriptor("generator").state_fields,
                                                {{"tones", {{{"freq_Hz", "not a frequency"}}}}});
    CHECK(bad_field.status == ParamWriteStatus::TypeMismatch);
    CHECK(bad_field.path == "tones[0].freq_Hz");
    CHECK(bad_field.expected == "number");

    const auto non_object =
        applyComponentParams(*engine, paramsDescriptor("generator").state_fields, {{"tones", {7}}});
    CHECK(non_object.status == ParamWriteStatus::TypeMismatch);
    CHECK(non_object.path == "tones[0]");
    CHECK(non_object.expected == "object");
}

TEST_CASE("Parameter validation reports the first error in request order", "[component_params]") {
    FakeParamsFixture fixture;
    auto &scalar_engine = fixture.create(1008, {{"gain_dB", 1.0}});
    const std::vector<ParameterField> scalar_fields = {testField("gain_dB")};

    nlohmann::ordered_json scalar_params = nlohmann::ordered_json::object();
    scalar_params["z_unknown"] = 1;
    scalar_params["a_unknown"] = 2;
    const auto scalar_result = applyComponentParams(scalar_engine, scalar_fields, scalar_params);
    CHECK(scalar_result.status == ParamWriteStatus::UnknownKey);
    CHECK(scalar_result.path == "z_unknown");

    auto &array_engine = fixture.create(1009, {{"tones", {{{"freq_Hz", 1e8}}}}});
    const std::vector<ParameterField> array_fields = {testField("tones[].freq_Hz")};
    nlohmann::ordered_json element = nlohmann::ordered_json::object();
    element["z_unknown"] = 1;
    element["freq_Hz"] = "not a frequency";
    nlohmann::ordered_json array_params = nlohmann::ordered_json::object();
    array_params["tones"] = nlohmann::ordered_json::array({element});

    const auto array_result = applyComponentParams(array_engine, array_fields, array_params);
    CHECK(array_result.status == ParamWriteStatus::UnknownKey);
    CHECK(array_result.path == "tones[0].z_unknown");
}

TEST_CASE("EditorCommands preserves parameter request order", "[component_params]") {
    ParamsCommandFixture fixture;
    const ComponentFactory factory = [](ComponentRegistry &components, NodeGraphEngine &graph,
                                        int id) -> IComponentEngine * {
        return &components.add<FakeParamsEngine>(id, graph, nlohmann::json{{"gain_dB", 1.0}});
    };
    auto *engine = fixture.commands.createComponent(factory);
    REQUIRE(engine != nullptr);
    const std::uint64_t revision = fixture.commands.revision();

    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["z_unknown"] = 1;
    params["a_unknown"] = 2;
    const auto result = fixture.commands.setComponentParams(engine->graphNodeId(), params);

    CHECK(result.status == ParamWriteStatus::UnknownKey);
    CHECK(result.path == "z_unknown");
    CHECK(fixture.commands.revision() == revision);
}

TEST_CASE("Enum labels and integer values store their integer indices", "[component_params]") {
    ParamsEngineFixture fixture;
    auto *engine = fixture.create("filter");
    REQUIRE(engine != nullptr);

    const auto accepted = applyComponentParams(*engine, paramsDescriptor("filter").state_fields,
                                               {{"filter_type", "BPF"}});
    CHECK(accepted.status == ParamWriteStatus::Applied);
    CHECK(engine->serialize().at("filter_type").get<int>() == 2);

    const auto integer_index = applyComponentParams(
        *engine, paramsDescriptor("filter").state_fields, {{"filter_type", 1}});
    CHECK(integer_index.status == ParamWriteStatus::Applied);
    CHECK(engine->serialize().at("filter_type").get<int>() == 1);

    const auto integral_float_index = applyComponentParams(
        *engine, paramsDescriptor("filter").state_fields, {{"filter_type", 2.0}});
    CHECK(integral_float_index.status == ParamWriteStatus::Applied);
    CHECK(engine->serialize().at("filter_type").get<int>() == 2);

    const auto out_of_range = applyComponentParams(*engine, paramsDescriptor("filter").state_fields,
                                                   {{"filter_type", 4}});
    CHECK(out_of_range.status == ParamWriteStatus::TypeMismatch);
    CHECK(out_of_range.expected == "one of LPF, HPF, BPF, BSF");

    const auto fractional = applyComponentParams(*engine, paramsDescriptor("filter").state_fields,
                                                 {{"filter_type", 1.5}});
    CHECK(fractional.status == ParamWriteStatus::TypeMismatch);
    CHECK(fractional.expected == "one of LPF, HPF, BPF, BSF");

    const auto rejected = applyComponentParams(*engine, paramsDescriptor("filter").state_fields,
                                               {{"filter_type", "XYZ"}});
    CHECK(rejected.status == ParamWriteStatus::TypeMismatch);
    CHECK(rejected.expected == "one of LPF, HPF, BPF, BSF");
}

TEST_CASE("Unknown parameter keys provide the nearest same-level suggestion",
          "[component_params]") {
    ParamsEngineFixture fixture;
    auto *engine = fixture.create("attenuator");
    REQUIRE(engine != nullptr);

    const auto result = applyComponentParams(*engine, paramsDescriptor("attenuator").state_fields,
                                             {{"attenuation_dB", 12.0}});
    CHECK(result.status == ParamWriteStatus::UnknownKey);
    CHECK(result.path == "attenuation_dB");
    REQUIRE_FALSE(result.suggestions.empty());
    CHECK(result.suggestions.front() == "atten_dB");
    CHECK(result.suggestions.size() <= 5);
}

TEST_CASE("S-parameter paths are rejected before other key validation", "[component_params]") {
    ParamsEngineFixture fixture;
    auto *engine = fixture.create("amplifier");
    REQUIRE(engine != nullptr);

    for (const std::string key : {"sparam_filepath", "sparam_path"}) {
        const auto result =
            applyComponentParams(*engine, paramsDescriptor("amplifier").state_fields, {{key, 123}});
        CHECK(result.status == ParamWriteStatus::PathParamUnsupported);
        CHECK(result.path == key);
    }
}

TEST_CASE("Read-only metadata is rejected before checking the JSON value type",
          "[component_params]") {
    FakeParamsFixture fixture;
    auto &engine = fixture.create(1002, {{"gain_dB", 1.0}});
    const std::vector<ParameterField> fields = {testField("gain_dB", FieldKind::Number, true)};

    const auto result = applyComponentParams(engine, fields, {{"gain_dB", "wrong type"}});
    CHECK(result.status == ParamWriteStatus::ReadOnly);
    CHECK(result.path == "gain_dB");
}

TEST_CASE("A no-op parameter write skips deserialize", "[component_params]") {
    FakeParamsFixture fixture;
    auto &engine = fixture.create(1003, {{"gain_dB", 5.0}});
    const std::vector<ParameterField> fields = {testField("gain_dB")};

    const auto result = applyComponentParams(engine, fields, {{"gain_dB", 5.0}});
    CHECK(result.status == ParamWriteStatus::Unchanged);
    CHECK(result.ok());
    CHECK(engine.deserialize_calls == 0);
}

TEST_CASE("Engine-adjusted PFB values roll back without advancing revision", "[component_params]") {
    ParamsCommandFixture fixture;
    auto *engine = fixture.commands.createComponent(paramsDescriptor("pfb").create);
    REQUIRE(engine != nullptr);
    const auto before = engine->serialize();
    const std::uint64_t revision = fixture.commands.revision();

    const auto result =
        fixture.commands.setComponentParams(engine->graphNodeId(), {{"channel_count", 4096}});
    CHECK(result.status == ParamWriteStatus::EngineAdjusted);
    CHECK(result.path == "channel_count");
    CHECK(result.requested == 4096);
    CHECK(result.stored == 2048);
    CHECK(engine->serialize() == before);
    CHECK(fixture.commands.revision() == revision);
}

TEST_CASE("Engine-induced changes outside a request are returned in path order",
          "[component_params]") {
    ParamsEngineFixture fixture;
    auto *engine = fixture.create("pfb");
    REQUIRE(engine != nullptr);
    const auto &fields = paramsDescriptor("pfb").state_fields;

    CHECK(applyComponentParams(*engine, fields, {{"active_channel", 20}}).status ==
          ParamWriteStatus::Applied);
    const auto result = applyComponentParams(*engine, fields, {{"channel_count", 16}});
    CHECK(result.status == ParamWriteStatus::Applied);
    REQUIRE(result.also_changed.size() == 1);
    CHECK(result.also_changed[0].path == "active_channel");
    CHECK(result.also_changed[0].old_value == 20);
    CHECK(result.also_changed[0].new_value == 15);
}

TEST_CASE("Small float normalization is tolerated but larger adjustment is rolled back",
          "[component_params]") {
    const std::vector<ParameterField> fields = {testField("gain_dB")};

    FakeParamsFixture close_fixture;
    auto &close_engine = close_fixture.create(1004, {{"gain_dB", 500.0}});
    close_engine.relative_adjustment = 1e-9;
    const auto close = applyComponentParams(close_engine, fields, {{"gain_dB", 1000.0}});
    CHECK(close.status == ParamWriteStatus::Applied);
    CHECK(close_engine.serialize().at("gain_dB").get<double>() > 1000.0);

    FakeParamsFixture adjusted_fixture;
    auto &adjusted_engine = adjusted_fixture.create(1005, {{"gain_dB", 500.0}});
    adjusted_engine.relative_adjustment = 1e-3;
    const auto adjusted = applyComponentParams(adjusted_engine, fields, {{"gain_dB", 1000.0}});
    CHECK(adjusted.status == ParamWriteStatus::EngineAdjusted);
    CHECK(adjusted.requested == 1000.0);
    CHECK(std::abs(adjusted.stored.get<double>() - 1001.0) < 1e-9);
    CHECK(adjusted_engine.serialize().at("gain_dB") == 500.0);
}

TEST_CASE("Deserialize failures restore the snapshot and report restore failures",
          "[component_params]") {
    const std::vector<ParameterField> fields = {testField("gain_dB")};

    FakeParamsFixture fixture;
    auto &engine = fixture.create(1006, {{"gain_dB", 5.0}});
    engine.throw_on_calls = {1};
    const auto failed = applyComponentParams(engine, fields, {{"gain_dB", 7.0}});
    CHECK(failed.status == ParamWriteStatus::DeserializeFailed);
    CHECK(engine.serialize().at("gain_dB") == 5.0);
    CHECK(engine.deserialize_calls == 2);

    auto &restore_engine = fixture.create(1007, {{"gain_dB", 5.0}});
    restore_engine.throw_on_calls = {1, 2};
    const auto restore_failed = applyComponentParams(restore_engine, fields, {{"gain_dB", 7.0}});
    CHECK(restore_failed.status == ParamWriteStatus::RestoreFailed);
    CHECK(restore_engine.deserialize_calls == 2);
}

TEST_CASE("Editor parameter writes advance revision only for applied changes",
          "[component_params]") {
    ParamsCommandFixture fixture;
    auto *engine = fixture.commands.createComponent(paramsDescriptor("amplifier").create);
    REQUIRE(engine != nullptr);
    fixture.commands.markClean();
    const std::uint64_t before = fixture.commands.revision();

    const auto applied =
        fixture.commands.setComponentParams(engine->graphNodeId(), {{"gain_dB", 12.0}});
    CHECK(applied.status == ParamWriteStatus::Applied);
    CHECK(fixture.commands.revision() == before + 1);

    const std::uint64_t after_applied = fixture.commands.revision();
    const auto unchanged =
        fixture.commands.setComponentParams(engine->graphNodeId(), {{"gain_dB", 12.0}});
    CHECK(unchanged.status == ParamWriteStatus::Unchanged);
    CHECK(fixture.commands.revision() == after_applied);

    const auto rejected =
        fixture.commands.setComponentParams(engine->graphNodeId(), {{"sparam_path", "x"}});
    CHECK(rejected.status == ParamWriteStatus::PathParamUnsupported);
    CHECK(fixture.commands.revision() == after_applied);

    const auto missing = fixture.commands.setComponentParams(987654, {{"gain_dB", 1.0}});
    CHECK(missing.status == ParamWriteStatus::UnknownComponent);
    CHECK(fixture.commands.revision() == after_applied);
}

TEST_CASE("Components without state metadata reject all parameter keys", "[component_params]") {
    ParamsCommandFixture fixture;
    const ComponentFactory factory = [](ComponentRegistry &registry, NodeGraphEngine &graph,
                                        int id) -> IComponentEngine * {
        return &registry.add<FakeParamsEngine>(id, graph, nlohmann::json{{"gain_dB", 1.0}});
    };
    auto *engine = fixture.commands.createComponent(factory);
    REQUIRE(engine != nullptr);
    const std::uint64_t revision = fixture.commands.revision();

    const auto result =
        fixture.commands.setComponentParams(engine->graphNodeId(), {{"gain_dB", 2.0}});
    CHECK(result.status == ParamWriteStatus::UnknownKey);
    CHECK(fixture.commands.revision() == revision);
}
