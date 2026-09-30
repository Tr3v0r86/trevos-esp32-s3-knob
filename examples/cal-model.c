#include "cal_model.h"
#include <assert.h>
#include <string.h>
int main(void) {
    static cal_window_t window;
    const char *json = "{\"days_list\":[{\"date\":\"2026-10-01\",\"events\":[]}]}";
    assert(cal_model_parse(json, strlen(json), &window));
    assert(cal_window_find(&window, "2026-10-01") != 0);
    return 0;
}
