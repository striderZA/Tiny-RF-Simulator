// Opt-in dirty/clean benchmark for PFB recompute at M=2048 (issue #130).
// Not registered with CTest: a K=64 recompute takes seconds in an optimised build.
// Build and run from the repo root:
//   cmake --build build --target bench_pfb_large
//   build/bin/bench_pfb_large --benchmark-samples 10
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include "spectrum.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

Spectrum makeLargeGridInput() {
    // 4097 bins over Fs=400 MHz.
    Spectrum in;
    in.frequencies.resize(4097);
    for (int i = 0; i < 4097; ++i)
        in.frequencies[i] = -200e6 + i * (400e6 / 4096.0);
    in.noise_total_W.assign(4097, 1e-20);
    return in;
}

} // namespace

TEST_CASE("Benchmark: PFBChannelizerEngine M=2048 recompute", "[bench][pfb_large]") {
    // Dirty = each update changes K (K and K-1 alternate), forcing
    // recomputeChannels(); clean = same input, cached channels.
    for (int K : {8, 64}) {
        SECTION("M=2048 K=" + std::to_string(K) + " dirty (K change forces recompute)") {
            NodeGraphEngine g;
            PFBChannelizerEngine pfb(0, g);
            pfb.setFs_Hz(400e6);
            pfb.setChannelCount(2048);
            pfb.setTapsPerBranch(K);
            Spectrum in = makeLargeGridInput();
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
            Spectrum in = makeLargeGridInput();
            pfb.node().inputs[0] = &in;
            pfb.update(0.0);
            BENCHMARK("PFB M=2048 K=" + std::to_string(K) + " cached") { pfb.update(0.0); };
        }
    }
}
