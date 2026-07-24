#pragma once

#include <complex>
#include <optional>
#include <string>
#include <vector>

struct TouchstoneData {
    int num_ports = 0;
    double reference_impedance = 50.0;

    enum class Format { DB, MA, RI };
    enum class Parameter { S, Y, Z, H, G };
    enum class FrequencyUnit { Hz, kHz, MHz, GHz };

    Format format = Format::MA;
    Parameter parameter = Parameter::S;
    FrequencyUnit freq_unit = FrequencyUnit::GHz;

    // Frequencies in Hz
    std::vector<double> frequencies;

    // Network parameters per frequency point.
    // For an n-port network, each frequency has n*n complex parameters
    // in row-major order: S11, S12, ..., S1n, S21, S22, ..., S2n, ..., Sn1, ..., Snn
    std::vector<std::vector<std::complex<double>>> parameters;

    double freqToHz(double freq) const;
};

class TouchstoneParser {
  public:
    static std::optional<TouchstoneData> parse(const std::string &filepath);

  private:
    static std::string stripComment(const std::string &line);
    static bool parseOptionLine(const std::string &line, TouchstoneData &data);
    static std::complex<double> parsePair(double a, double b, TouchstoneData::Format fmt);
};
