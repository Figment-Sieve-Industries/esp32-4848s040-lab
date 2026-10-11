// STUB (contract placeholder): replaced by the real implementation. See day_history.h.
#include "day_history.h"
#include <stdio.h>
#include <string.h>

namespace dayhist {

namespace {
View s_view;
} // namespace

void dayHistoryUpdate(const PwData &, const TodayStats &) {}

void dayHistoryRequest(Mode mode, int offset) {
    if (s_view.mode == mode && s_view.offset == offset && s_view.count) return;
    s_view = View();
    s_view.seq = 1;
    s_view.mode = mode;
    s_view.offset = offset;
    s_view.canNext = offset < 0;
    s_view.count = mode == Mode::Day ? 24 : mode == Mode::Week ? 7 : 31;
    strncpy(s_view.label, mode == Mode::Day ? "Today" : mode == Mode::Week ? "This week" : "This month",
            sizeof(s_view.label) - 1);
    memset(s_view.soc, SOC_NONE, sizeof(s_view.soc));
}

void dayHistoryView(View &out) {
    if (!s_view.count) dayHistoryRequest(Mode::Day, 0);
    out = s_view;
}

void dayHistorySdTick(bool) {}

void dayHistoryStatusLine(char *buf, size_t n) { snprintf(buf, n, "day history: stub"); }

} // namespace dayhist
