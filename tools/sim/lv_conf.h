// The simulator uses the device's LVGL configuration, with one change. LVGL
// keeps its own small heap (32 KB on the device). A 64-bit desktop needs about
// twice that for the same objects, so the simulator gets 64 KB: enough to
// behave like the device, small enough that a plugin which would exhaust the
// device's heap fails here too.
#include "../../include/lv_conf.h"

#undef LV_MEM_SIZE
#define LV_MEM_SIZE (64 * 1024U)
