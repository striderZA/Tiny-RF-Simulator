#include "amplifier_engine.h"
#include "common.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include "splitter_engine.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Benchmark: SignalGeneratorEngine update", "[bench][generator]") {
    SECTION("first call (dirty)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(0, g);
        gen.addTone(100e6, -20.0);
        gen.addTone(200e6, -10.0);
        BENCHMARK("Generator update dirty") { gen.update(0.0); };
    }

    SECTION("subsequent call (clean, dirty flag skips work)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(0, g);
        gen.addTone(100e6, -20.0);
        gen.addTone(200e6, -10.0);
        gen.update(0.0); // prime: sets m_dirty=false
        BENCHMARK("Generator update clean (dirty skip)") { gen.update(0.0); };
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
        BENCHMARK("Amplifier update dirty") { amp.update(0.0); };
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
        BENCHMARK("Amplifier update clean (dirty skip)") { amp.update(0.0); };
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
        BENCHMARK("Mixer update dirty") { mix.update(0.0); };
    }

    SECTION("subsequent call (dirty skip)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(2, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        MixerEngine mix(0, g);
        mix.node().inputs[0] = &gen.node().outputs[0];
        mix.update(0.0); // prime
        BENCHMARK("Mixer update clean (dirty skip)") { mix.update(0.0); };
    }
}

TEST_CASE("Benchmark: SplitterEngine update", "[bench][splitter]") {
    SECTION("first call (dirty)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(3, g);
        gen.update(0.0);
        SplitterEngine split(0, g);
        split.node().inputs[0] = &gen.node().outputs[0];
        BENCHMARK("Splitter update dirty") { split.update(0.0); };
    }

    SECTION("subsequent call (dirty skip)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(3, g);
        gen.update(0.0);
        SplitterEngine split(0, g);
        split.node().inputs[0] = &gen.node().outputs[0];
        split.update(0.0); // prime
        BENCHMARK("Splitter update clean (dirty skip)") { split.update(0.0); };
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

        BENCHMARK("PFB update first call") { pfb.update(0.0); };
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
        BENCHMARK("PFB update cached grid") { pfb.update(0.0); };
    }

    // Issue #130 (PFB recompute): M=2048 with a 4097-bin grid over Fs=400 MHz.
    // Dirty = each update changes K (K and K-1 alternate), forcing
    // recomputeChannels(); clean = same input, cached channels. A K=64 dirty
    // iteration takes seconds in an optimised build, so run these with
    // `build/bin/tests "Benchmark: PFBChannelizerEngine update" --benchmark-samples 10`.
    for (int K : {8, 64}) {
        SECTION("M=2048 K=" + std::to_string(K) + " dirty (K change forces recompute)") {
            NodeGraphEngine g;
            PFBChannelizerEngine pfb(0, g);
            pfb.setFs_Hz(400e6);
            pfb.setChannelCount(2048);
            pfb.setTapsPerBranch(K);
            Spectrum in;
            in.frequencies.resize(4097);
            for (int i = 0; i < 4097; ++i)
                in.frequencies[i] = -200e6 + i * (400e6 / 4096.0);
            in.noise_total_W.assign(4097, 1e-20);
            pfb.node().inputs[0] = &in;
            pfb.update(0.0);
            bool flip = false;
            BENCHMARK("PFB M=2048 K=" + std::to_string(K) + " recompute (K toggles K/K-1)") {
                flip = !flip;
                pfb.setTapsPerBranch(flip ? K : K - 1);
                pfb.update(0.0);
            };
        }
        SECTION("M=2048 K=" + std::to_string(K) + " clean (cached channels)") {
            NodeGraphEngine g;
            PFBChannelizerEngine pfb(0, g);
            pfb.setFs_Hz(400e6);
            pfb.setChannelCount(2048);
            pfb.setTapsPerBranch(K);
            Spectrum in;
            in.frequencies.resize(4097);
            for (int i = 0; i < 4097; ++i)
                in.frequencies[i] = -200e6 + i * (400e6 / 4096.0);
            in.noise_total_W.assign(4097, 1e-20);
            pfb.node().inputs[0] = &in;
            pfb.update(0.0);
            BENCHMARK("PFB M=2048 K=" + std::to_string(K) + " cached") { pfb.update(0.0); };
        }
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
        BENCHMARK("renderSpectrum first call") { sa.renderSpectrum(gen.node().outputs[0]); };
    }

    SECTION("second call (RBW cache hit)") {
        NodeGraphEngine g;
        SignalGeneratorEngine gen(4, g);
        gen.addTone(100e6, -20.0);
        gen.update(0.0);
        SpectrumAnalyzerEngine sa;
        sa.setNoiseJitterEnabled(false);
        sa.renderSpectrum(gen.node().outputs[0]); // prime cache
        BENCHMARK("renderSpectrum cached RBW") { sa.renderSpectrum(gen.node().outputs[0]); };
    }
}
