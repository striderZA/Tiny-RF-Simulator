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

    int to_delete = -1;
    for (int i = 0; i < static_cast<int>(m_engines.size()); ++i) {
        auto& adc = m_engines[i];
        ImGui::PushID(i);

        auto label = "ADC " + std::to_string(adc->id());
        if (ImGui::TreeNode(label.c_str())) {
            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, 140.0f);

            ImGui::Text("Fs (MHz)");
            ImGui::NextColumn();
            double fs = adc->fs_Hz();
            if (utils::inputFrequency("##fs", fs, 1.0, 100.0, "%.0f", 1e3, 1e12))
                adc->setFs_Hz(fs);
            ImGui::NextColumn();

            ImGui::Text("NSD (dBm/Hz)");
            ImGui::NextColumn();
            double nsd = adc->nsd_dBm_per_Hz();
            if (utils::inputDouble("##nsd", nsd, 1, 10, "%.1f", -250.0, -30.0))
                adc->setNsd_dBm_per_Hz(nsd);
            ImGui::NextColumn();

            ImGui::Text("Bits");
            ImGui::NextColumn();
            int bits = adc->bits();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt("##bits", &bits)) {
                if (bits < 1) bits = 1;
                if (bits > 24) bits = 24;
                adc->setBits(bits);
            }
            ImGui::NextColumn();

            ImGui::Text("V_FS (V)");
            ImGui::NextColumn();
            double vfs = adc->v_fs();
            ImGui::SetNextItemWidth(120.0f);
            if (utils::inputDouble("##vfs", vfs, 0.1, 1.0, "%.2f", 0.1, 10.0))
                adc->setVfs(vfs);
            ImGui::NextColumn();

            ImGui::Text("f_chan (MHz)");
            ImGui::NextColumn();
            double fchan = adc->fChannel_Hz();
            if (utils::inputFrequency("##fchan", fchan, 1.0, 100.0, "%.3f", 0.0, 1e12))
                adc->setFChannel_Hz(fchan);
            ImGui::NextColumn();

            ImGui::Text("Decimation");
            ImGui::NextColumn();
            int decim = adc->decimation();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt("##decim", &decim)) {
                if (decim < 1) decim = 1;
                adc->setDecimation(decim);
            }
            ImGui::NextColumn();

            ImGui::Text("N samples");
            ImGui::NextColumn();
            int ns = adc->nSamples();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt("##ns", &ns)) {
                if (ns < adc->decimation()) ns = adc->decimation();
                adc->setNSamples(ns);
            }
            ImGui::NextColumn();

            ImGui::Columns(1);
            if (ImGui::Button("Delete"))
                to_delete = i;

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (to_delete >= 0) {
        LOG_INFO("Remove ADC [adc%d].", m_engines[to_delete]->id());
        onRemoveAdc(static_cast<size_t>(to_delete));
    }

    if (ImGui::Button("+ Add ADC"))
        onAddAdc();

    ImGui::End();
}
