#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char **argv) {
    if (const char *mode = std::getenv("RFSIM_GIT_STATUS_HELPER_MODE");
        mode != nullptr && std::string_view(mode) == "delayed-status") {
        const std::filesystem::path directory = std::filesystem::current_path();
        std::ifstream branch_file(directory / "git_status_branch.txt");
        std::string branch;
        if (!std::getline(branch_file, branch))
            return 5;
        {
            std::ofstream started_file(directory / "git_status_helper_started");
            started_file << "started";
            if (!started_file.good())
                return 6;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "# branch.oid 0123456789abcdef0123456789abcdef01234567\n"
                  << "# branch.head " << branch << '\n';
        return std::cout.good() ? 0 : 7;
    }
#ifdef _WIN32
    if (argc > 1 && std::string_view(argv[1]) == "--child") {
        Sleep(5000);
        return 0;
    }

    std::array<wchar_t, 32768> executable_path{};
    const DWORD path_length = GetModuleFileNameW(nullptr, executable_path.data(),
                                                 static_cast<DWORD>(executable_path.size()));
    if (path_length == 0 || path_length >= executable_path.size())
        return 2;

    std::wstring command_line =
        L"\"" + std::wstring(executable_path.data(), path_length) + L"\" --child";
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(executable_path.data(), command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup_info, &process_info))
        return 3;

    {
        std::ofstream pid_file("git_status_child.pid");
        pid_file << process_info.dwProcessId;
        if (!pid_file.good())
            return 4;
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
#else
    (void)argc;
    (void)argv;
#endif

    std::this_thread::sleep_for(std::chrono::seconds(5));
    return 0;
}
