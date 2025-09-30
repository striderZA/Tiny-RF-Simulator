#pragma once

#include "imgui.h"
#include <vector>
#include <string>
#include <chrono>
#include <ctime>

struct AppLog {
	ImGuiTextBuffer buf;
	ImGuiTextFilter filter;
	ImVector<int> line_offsets;
	bool auto_scroll;
	bool show_info;
	bool show_warn;
	bool show_error;

	AppLog() {
		auto_scroll = true;
		show_info = show_warn = show_error = true;
		Clear();
	}

	void Clear() {
		buf.clear();
		line_offsets.clear();
		buf.appendf("=== Log started at %s ===\n", ImGui::GetVersion());
		line_offsets.push_back(0);
	}

	static std::string GetCurrentTimestamp() {
		auto now = std::chrono::system_clock::now();
		std::time_t now_time = std::chrono::system_clock::to_time_t(now);
		char buffer[32];
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
		return std::string(buffer);
	}

	void AddLog(const char* fmt, ...) IM_FMTARGS(2) {
		int old_size = buf.size();
		va_list args;
		va_start(args, fmt);
		// Prepend timestamp
		std::string timestamp = GetCurrentTimestamp() + " ";
		buf.appendf("%s", timestamp.c_str());
		buf.appendfv(fmt, args);
		va_end(args);

		for (int new_size = buf.size(); old_size < new_size; old_size++)
			if (buf[old_size] == '\n')
				line_offsets.push_back(old_size + 1);
	}

	void Draw(const char* title, bool* p_open = nullptr) {
		if (!ImGui::Begin(title, p_open)) {
			ImGui::End();
			return;
		}

		// Options menu
		if (ImGui::BeginPopup("Options")) {
			ImGui::Checkbox("Auto-scroll", &auto_scroll);
			ImGui::EndPopup();
		}

		// Main window
		if (ImGui::Button("Options"))
			ImGui::OpenPopup("Options");
		ImGui::SameLine();
		bool clear = ImGui::Button("Clear");
		ImGui::SameLine();
		bool copy = ImGui::Button("Copy");
		ImGui::SameLine();
		filter.Draw("Filter (inc,-exc)", -100.0f);
		ImGui::Separator();

		if (clear)
			Clear();
		if (copy)
			ImGui::LogToClipboard();

		ImGui::BeginChild("##scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		if (filter.IsActive()) {
			for (int line_no = 0; line_no < line_offsets.Size; line_no++) {
				const char* line_start = buf.begin() + line_offsets[line_no];
				const char* line_end = (line_no + 1 < line_offsets.Size) ? (buf.begin() + line_offsets[line_no + 1] - 1) : buf.end();
				if (filter.PassFilter(line_start, line_end)) {
					bool is_error = strstr(line_start, "[ERROR]") != nullptr;
					bool is_warn = strstr(line_start, "[WARN]") != nullptr;
					bool is_info = strstr(line_start, "[INFO]") != nullptr;
					ImVec4 color = is_error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : (is_warn ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
					if (!show_error && is_error) continue;
					if (!show_warn && is_warn) continue;
					if (!show_info && is_info) continue;
					ImGui::PushStyleColor(ImGuiCol_Text, color);
					ImGui::TextUnformatted(line_start, line_end);
					ImGui::PopStyleColor();
				}
			}
		}
		else {
			// Full text view
			ImGui::TextUnformatted(buf.begin());
		}

		if (auto_scroll && ImGui::GetScrollY() >= (ImGui::GetScrollMaxY() * 0.9f))
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();
		ImGui::End();
	}
};

extern AppLog GAppLog;

// Logging macros for convenience (use like printf)
#define LOG_INFO(fmt, ...)  do { GAppLog.AddLog("[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { GAppLog.AddLog("[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmt, ...) do { GAppLog.AddLog("[ERROR] " fmt "\n", ##__VA_ARGS__); } while (0)

// Function to show the log window
void ShowAppLog(bool* p_open = nullptr);