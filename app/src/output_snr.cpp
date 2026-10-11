#include "output_snr.h"

#include "component_interface.h"
#include "pfb_channelizer_engine.h"
#include "spectrum_analyzer_engine.h"
#include <cmath>
#include <cstddef>

OutputSnr computeOutputSnr(const IComponentEngine &component, int output_port,
                           const SpectrumAnalyzerEngine &analyzer) {
    OutputSnr result;
    const auto &outputs = component.node().outputs;
    if (output_port < 0 || static_cast<std::size_t>(output_port) >= outputs.size())
        return result;

    if (output_port == 0) {
        if (const auto *pfb = dynamic_cast<const PFBChannelizerEngine *>(&component)) {
            result.basis = SnrBasis::PfbChannel;
            result.snr_dB = pfb->computeActiveChannelSNRdB();

            const int active_channel = pfb->activeChannel();
            const auto &channels = pfb->channels();
            if (active_channel >= 0 && static_cast<std::size_t>(active_channel) < channels.size()) {
                const auto &channel = channels[static_cast<std::size_t>(active_channel)];
                result.enbw_Hz = channel.enbw_Hz;
                if (channel.noise_W > 0.0 && std::isfinite(channel.noise_W))
                    result.channel_noise_dBm = 10.0 * std::log10(channel.noise_W) + 30.0;
            }
            return result;
        }
    }

    result.rbw_Hz = analyzer.rbw();
    result.snr_dB =
        analyzer.computeStrongestToneSNRdB(outputs[static_cast<std::size_t>(output_port)]);
    return result;
}
