#include "mcp_bridge.h"

#include "agent_endpoint.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifndef RFSIM_APP_VERSION
#define RFSIM_APP_VERSION "unknown"
#endif

namespace {

void printUsage(std::ostream &out) {
    out << "Usage: rf-sim-mcp [--endpoint <file>] [--version] [--help]\n";
}

} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    const int stdin_mode = _setmode(_fileno(stdin), _O_BINARY);
    const int stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
    if (stdin_mode == -1 || stdout_mode == -1) {
        std::cerr << "rf-sim-mcp: failed to set binary stdin/stdout mode\n";
        return 1;
    }
#endif

    BridgeOptions options;
    options.server_version = RFSIM_APP_VERSION;
    bool show_help = false;
    bool show_version = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--endpoint") {
            if (index + 1 >= argc) {
                std::cerr << "rf-sim-mcp: --endpoint requires a file path\n";
                printUsage(std::cerr);
                return 2;
            }
            options.endpoint_file = std::filesystem::path(argv[++index]);
        } else if (argument == "--version") {
            show_version = true;
        } else if (argument == "--help") {
            show_help = true;
        } else {
            std::cerr << "rf-sim-mcp: unknown argument: " << argument << '\n';
            printUsage(std::cerr);
            return 2;
        }
    }

    if (show_help) {
        printUsage(std::cout);
        return 0;
    }
    if (show_version) {
        std::cout << "rf-sim-mcp " << RFSIM_APP_VERSION << '\n';
        return 0;
    }

    try {
        options.exe_dir = currentExecutableDirectory();
    } catch (const std::exception &error) {
        std::cerr << "rf-sim-mcp: " << error.what() << '\n';
        return 1;
    }

    return runBridge(std::cin, std::cout, std::cerr, options);
}
