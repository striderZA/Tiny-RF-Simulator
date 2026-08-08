#include "tutorial_state.h"

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

// Mirrors LayoutManager::detectExeDir — completion must be recorded next to the
// executable, not wherever the process happened to be started from.
std::string detectExeDir() {
    std::string exe_path;
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        exe_path = buf;
#elif defined(__APPLE__)
    char buf[PATH_MAX] = {};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        exe_path = buf;
#else
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        exe_path = buf;
    }
#endif
    if (exe_path.empty())
        return fs::current_path().string();

    fs::path parent = fs::path(exe_path).parent_path();
    if (parent.empty())
        return fs::current_path().string();
    return parent.string();
}

} // namespace

TutorialState::TutorialState() : m_exe_dir(detectExeDir()) {}

std::string TutorialState::markerPath() const {
    return (fs::path(m_exe_dir) / ".tutorial_completed").string();
}

bool TutorialState::completed() const {
    std::error_code ec;
    return fs::exists(markerPath(), ec);
}

void TutorialState::markCompleted() {
    // Existence is the whole signal — the file's contents are never read.
    std::ofstream ofs(markerPath(), std::ios::app);
}

void TutorialState::start() {
    m_step_index = 0;
    m_active = true;
}

void TutorialState::next() {
    if (atLastStep()) {
        markCompleted();
        m_active = false;
        return;
    }
    ++m_step_index;
}

void TutorialState::back() {
    if (m_step_index > 0)
        --m_step_index;
}

void TutorialState::skipToLast() {
    int last = stepCount() - 1;
    if (last > m_step_index)
        m_step_index = last;
}

void TutorialState::exit() { m_active = false; }

bool TutorialState::atLastStep() const { return m_step_index >= stepCount() - 1; }

const TutorialStep &TutorialState::currentStep() const {
    const auto &steps = tutorialSteps();
    size_t i = static_cast<size_t>(m_step_index);
    return steps[i < steps.size() ? i : steps.size() - 1];
}
