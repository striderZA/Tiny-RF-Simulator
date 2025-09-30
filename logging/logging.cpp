#include "logging.h"

AppLog GAppLog;

void ShowAppLog(bool* p_open) {
	GAppLog.Draw("Log", p_open);
}