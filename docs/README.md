# Documentation

Engineering references and design specs that support the codebase.

## Resources

| Document | Description |
|----------|-------------|
| [amplifier_nonlinear_model.md](resources/amplifier_nonlinear_model.md) | Nonlinear amplifier model (gain compression, harmonics, IMD) — engineering reference for the amplifier module |
| [pfb_channelizer_info.md](resources/pfb_channelizer_info.md) | Polyphase filter bank channelizer — theory and design notes for the PFB module |
| [rf_adc_info.md](resources/rf_adc_info.md) | RF ADC software twin — semi-idealized sampling model (aliasing, Nyquist zones, NSD) |
| [touchstone_v2_parser_spec.md](resources/touchstone_v2_parser_spec.md) | Touchstone v2.0 file format spec — parser reference for `.s2p`/`.s3p`/`.s4p` support |

## Adding a new doc

Drop a new Markdown file in `resources/` and link it from the table above. Use a lowercase, hyphen-separated filename that matches the topic.
