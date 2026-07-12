<p align="center">
  <img src="assets/banner.png" alt="RF Simulator Banner" width="70%">
</p>

# RF Simulator

Modular RF signal chain simulator with real-time spectrum display using Dear ImGui + ImPlot.
Design a cascade of RF components and probe any node to see the spectrum.

[![Build & Test](https://github.com/striderZA/Tiny-RF-Simulator/actions/workflows/build.yml/badge.svg)](https://github.com/striderZA/Tiny-RF-Simulator/actions/workflows/build.yml)

## Quick Start

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
build/bin/tiny-rf-simulator.exe
```

**First build** takes 60-90s (FetchContent downloads all dependencies). For detailed setup, prerequisites, and per-platform instructions see the [Quickstart Guide](openwiki/quickstart.md).

## Documentation

| Topic | Link |
|---|---|
| Quickstart & Build | [Quickstart Guide](openwiki/quickstart.md) |
| Architecture | [Architecture Overview](openwiki/architecture/overview.md) |
| Components | [RF Components Reference](openwiki/domains/rf-components.md) |
| S-Parameter System | [S-Parameter System](openwiki/integrations/s-param-system.md) |
| Testing | [Testing Guide](openwiki/testing/guidance.md) |
| Operations | [Build & Operations](openwiki/operations/build-runbook.md) |
| DSP Pipeline | [DSP Pipeline & Workflows](openwiki/workflows/dsp-pipeline.md) |
| Engineering Refs | [Amplifier nonlinear model](docs/resources/amplifier_nonlinear_model.md), [PFB channelizer](docs/resources/pfb_channelizer_info.md), [RF ADC](docs/resources/rf_adc_info.md), [Touchstone parser spec](docs/resources/touchstone_v2_parser_spec.md) |
| Roadmap | [ROADMAP.md](ROADMAP.md) |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), the [OpenWiki](openwiki/), and [AGENTS.md](AGENTS.md) for project conventions.

## License

[MIT](LICENSE)
