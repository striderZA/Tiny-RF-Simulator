#include "logging_widget.h"
#include "implot.h"

void LoggingWidget::draw(const char* title, bool* p_open) {
	if (!ImGui::Begin(title, p_open)) {
		ImGui::End();
		return;
	}

	auto& log = simulator::log::LoggerCore::instance();

	if (ImGui::BeginPopup("Options")) {
		ImGui::Checkbox("Auto-scroll", &auto_scroll);
		ImGui::Checkbox("Show INFO", &show_info);
		ImGui::Checkbox("Show WARN", &show_warn);
		ImGui::Checkbox("Show ERROR", &show_error);
		ImGui::EndPopup();
	}

	if (ImGui::Button("Options")) ImGui::OpenPopup("Options");
	ImGui::SameLine();
	filter.Draw("Filter (inc,-exc)");

	ImGui::Separator();

	ImGui::BeginChild("scrolling");

	for (auto& entry : log.entries()) {
		if (!show_info && entry.level == simulator::log::Level::Info)  continue;
		if (!show_warn && entry.level == simulator::log::Level::Warn)  continue;
		if (!show_error && entry.level == simulator::log::Level::Error) continue;

		std::string line = entry.timestamp + " ";

		switch (entry.level) {
		case simulator::log::Level::Info:  line += "[INFO] "; break;
		case simulator::log::Level::Warn:  line += "[WARN] "; break;
		case simulator::log::Level::Error: line += "[ERROR] "; break;
		}

		line += entry.message;

		if (!filter.PassFilter(line.c_str()))
			continue;

		ImVec4 color = ImVec4(1, 1, 1, 1);
		if (entry.level == simulator::log::Level::Warn)  color = ImVec4(1, 1, 0, 1);
		if (entry.level == simulator::log::Level::Error) color = ImVec4(1, 0.4f, 0.4f, 1);

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(line.c_str());
		ImGui::PopStyleColor();
	}

	if (auto_scroll)
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::End();
}