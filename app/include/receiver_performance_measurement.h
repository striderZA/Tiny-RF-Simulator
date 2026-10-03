#pragma once

#include "receiver_requirements.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class IMeasurementChainHost;
class NodeGraphEngine;

class IsolatedChainRunner;

struct ReceiverPerformanceMeasurements {
    std::vector<double> output_power_dBm;
    std::vector<double> iip3_dBm;
};

class ReceiverPerformanceMeasurementEngine {
  public:
    ReceiverPerformanceMeasurementEngine(const NodeGraphEngine &graph, IMeasurementChainHost &host);
    ~ReceiverPerformanceMeasurementEngine();

    void update(const ReceiverRequirementsConfig &config, int point_a_pin, int point_b_pin,
                const std::vector<double> &sweep_frequencies_Hz);
    bool isInProgress() const { return m_in_progress; }
    const ReceiverPerformanceMeasurements &measurements() const { return m_measurements; }

  private:
    const NodeGraphEngine &m_graph;
    IMeasurementChainHost &m_host;
    ReceiverPerformanceMeasurements m_measurements;
    std::string m_cached_request;
    bool m_has_cached_request = false;
    std::unique_ptr<IsolatedChainRunner> m_runner;
    std::vector<double> m_sweep_frequencies_Hz;
    std::optional<std::pair<double, double>> m_output_tone_power_phase;
    std::optional<ReceiverIIP3TestSettings> m_iip3_settings;
    std::optional<std::size_t> m_iip3_level_count;
    std::size_t m_current_center = 0;
    std::size_t m_current_iip3_level = 0;
    enum class CenterStage { OutputTone, IIP3 };
    CenterStage m_center_stage = CenterStage::OutputTone;
    std::vector<std::pair<double, double>> m_lower_fundamental;
    std::vector<std::pair<double, double>> m_upper_fundamental;
    std::vector<std::pair<double, double>> m_lower_im3;
    std::vector<std::pair<double, double>> m_upper_im3;
    std::uint64_t m_spectrum_generation = 0;
    bool m_in_progress = false;
};
