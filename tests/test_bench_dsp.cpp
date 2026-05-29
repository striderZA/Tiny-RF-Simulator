#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "common.h"
#include "spectrum.h"
#include "signal_generator_engine.h"
#include "amplifier_engine.h"
#include "mixer_engine.h"
#include "splitter_engine.h"
#include "spectrum_analyzer_engine.h"
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"

TEST_CASE("Benchmark: SignalGeneratorEngine update", "[bench][generator]") {
    SECTION("first call (dirty)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(0, g);
        gen.addTone(100e6, -20.0);
        gen.addTone(200e6, -10.0);
        BENCHMARK("Generator update dirty") {
            gen.update(0.0);
        };
    }

    SECTION("subsequent call (clean, dirty flag skips work)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(0, g);
        gen.addTone(100e6, -20.0);
        gen.addTone(200e6, -10.0);
        gen.update(0.0); // prime: sets m_dirty=false
        BENCHMARK("Generator update clean (dirty skip)") {
            gen.update(0.0);
        };
    }
}

TEST_CASE("Benchmark: AmplifierEngine update", "[bench][amplifier]") {
    SECTION("first call (dirty)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(1, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        AmplifierEngine amp(0, g);
        amp.setGain_dB(10.0);
        amp.node().inputs[0] = &gen.node().outputs[0];
        BENCHMARK("Amplifier update dirty") {
            amp.update(0.0);
        };
    }

    SECTION("subsequent call with same input (dirty skip)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(1, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        AmplifierEngine amp(0, g);
        amp.setGain_dB(10.0);
        amp.node().inputs[0] = &gen.node().outputs[0];
        amp.update(0.0); // prime
        BENCHMARK("Amplifier update clean (dirty skip)") {
            amp.update(0.0);
        };
    }
}

TEST_CASE("Benchmark: MixerEngine update", "[bench][mixer]") {
    SECTION("first call (dirty)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(2, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        MixerEngine mix(0, g);
        mix.node().inputs[0] = &gen.node().outputs[0];
        BENCHMARK("Mixer update dirty") {
            mix.update(0.0);
        };
    }

    SECTION("subsequent call (dirty skip)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(2, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        MixerEngine mix(0, g);
        mix.node().inputs[0] = &gen.node().outputs[0];
        mix.update(0.0); // prime
        BENCHMARK("Mixer update clean (dirty skip)") {
            mix.update(0.0);
        };
    }
}

TEST_CASE("Benchmark: SplitterEngine update", "[bench][splitter]") {
    SECTION("first call (dirty)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(3, g);
        gen.update(0.0);
        SplitterEngine split(0, g);
        split.node().inputs[0] = &gen.node().outputs[0];
        BENCHMARK("Splitter update dirty") {
            split.update(0.0);
        };
    }

    SECTION("subsequent call (dirty skip)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(3, g);
        gen.update(0.0);
        SplitterEngine split(0, g);
        split.node().inputs[0] = &gen.node().outputs[0];
        split.update(0.0); // prime
        BENCHMARK("Splitter update clean (dirty skip)") {
            split.update(0.0);
        };
    }
}

TEST_CASE("Benchmark: PFBChannelizerEngine update", "[bench][pfb]") {
    SECTION("first call (channels recomputed)") {
        NodeGraphEngine g;
        PFBChannelizerEngine pfb(0, g);
        pfb.setFs_Hz(400e6);

        Spectrum in;
        in.frequencies.resize(401);
        for (int i = 0; i < 401; ++i)
            in.frequencies[i] = -200e6 + i * 1e6;
        in.noise_total_W.assign(401, 1e-20);

        pfb.node().inputs[0] = &in;

        BENCHMARK("PFB update first call") {
            pfb.update(0.0);
        };
    }

    SECTION("second call with same input (channels cached)") {
        NodeGraphEngine g;
        PFBChannelizerEngine pfb(0, g);
        pfb.setFs_Hz(400e6);

        Spectrum in;
        in.frequencies.resize(401);
        for (int i = 0; i < 401; ++i)
            in.frequencies[i] = -200e6 + i * 1e6;
        in.noise_total_W.assign(401, 1e-20);

        pfb.node().inputs[0] = &in;
        pfb.update(0.0); // prime: channels cached
        BENCHMARK("PFB update cached grid") {
            pfb.update(0.0);
        };
    }
}

TEST_CASE("Benchmark: SpectrumAnalyzerEngine renderSpectrum", "[bench][spectrum]") {
    SECTION("first call (RBW cache miss)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(4, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        BENCHMARK("renderSpectrum first call") {
            sa.renderSpectrum(gen.node().outputs[0]);
        };
    }

    SECTION("second call (RBW cache hit)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(4, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        sa.renderSpectrum(gen.node().outputs[0]); // prime cache
        BENCHMARK("renderSpectrum cached RBW") {
            sa.renderSpectrum(gen.node().outputs[0]);
        };
    }
}
