#include "adc_widget.h"

#include <imgui.h>

#include "adc_engine.h"
#include "logging_core.h"
#include "utils.h"

AdcWidget::AdcWidget(std::vector<std::unique_ptr<AdcEngine>>& engines)
    : m_engines(engines) {}

void AdcWidget::draw(const char* title, bool* p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("RF ADCs");

    if (ImGui::BeginTable("adcs", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Fs (MHz)");
        ImGui::TableSetupColumn("NSD (dBm/Hz)");
        ImGui::TableSetupColumn("Bits");
        ImGui::TableSetupColumn("V_FS (V)");
        ImGui::TableSetupColumn("f_chan (MHz)");
        ImGui::TableSetupColumn("BW (MHz)");
        ImGui::TableSetupColumn("D");
        ImGui::TableSetupColumn("N");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();

        int to_delete = -1;
        for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
            auto& adc = m_engines[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", adc->id());

            ImGui::TableSetColumnIndex(1);
            double fs = adc->fs_Hz();
            if (utils::inputFrequency("##fs", fs, 1.0, 100.0, "%.0f", 1e6, 100e9))
                adc->setFs_Hz(fs);

            ImGui::TableSetColumnIndex(2);
            double nsd = adc->nsd_dBm_per_Hz();
            if (utils::inputDouble("##nsd", nsd, 1, 10, "%.1f", -200.0, -50.0))
                adc->setNsd_dBm_per_Hz(nsd);

            ImGui::TableSetColumnIndex(3);
            int bits = adc->bits();
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::InputInt("##bits", &bits)) {
                if (bits < 1) bits = 1;
                if (bits > 24) bits = 24;
                adc->setBits(bits);
            }

            ImGui::TableSetColumnIndex(4);
            double vfs = adc->v_fs();
            ImGui::SetNextItemWidth(60.0f);
            if (utils::inputDouble("##vfs", vfs, 0.1, 1.0, "%.2f", 0.1, 10.0))
                adc->setVfs(vfs);

            ImGui::TableSetColumnIndex(5);
            double fchan = adc->fChannel_Hz();
            if (utils::inputFrequency("##fchan", fchan, 1.0, 100.0, "%.3f", 0.0, 100e9))
                adc->setFChannel_Hz(fchan);

            ImGui::TableSetColumnIndex(6);
            double bw = adc->bw_Hz();
            if (utils::inputFrequency("##bw", bw, 0.1, 10.0, "%.0f", 0.0, 1e9))
                adc->setBw_Hz(bw);

            ImGui::TableSetColumnIndex(7);
            int decim = adc->decimation();
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::InputInt("##decim", &decim)) {
                if (decim < 1) decim = 1;
                adc->setDecimation(decim);
            }

            ImGui::TableSetColumnIndex(8);
            int ns = adc->nSamples();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::InputInt("##ns", &ns)) {
                if (ns < adc->decimation()) ns = adc->decimation();
                adc->setNSamples(ns);
            }

            ImGui::TableSetColumnIndex(9);
            if (ImGui::SmallButton("X"))
                to_delete = i;

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (to_delete >= 0) {
            LOG_INFO("Remove ADC [adc%d].", m_engines[to_delete]->id());
            onRemoveAdc(static_cast<size_t>(to_delete));
        }
    }

    if (ImGui::Button("+ Add ADC"))
        onAddAdc();

    ImGui::End();
}
