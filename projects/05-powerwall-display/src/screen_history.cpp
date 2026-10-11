// STUB (contract placeholder): the history screen (day / week / month charts). See screens.h.
#include "screens.h"
#include "ui_common.h"

namespace {
lv_obj_t *s_scr = nullptr;
} // namespace

lv_obj_t *historyCreate() {
    s_scr = ui::screenCreate(ui::SCR_HISTORY);
    return s_scr;
}
void historyRefresh() {}
void historyOnShow() {}
bool historySetMode(int) { return true; }
bool historyStep(int) { return true; }
void historyDescribe(char *buf, size_t n) {
    if (n) buf[0] = '\0';
}
