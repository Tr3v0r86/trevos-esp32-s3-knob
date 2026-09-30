#pragma once
#include "trev_app.h"
extern const trev_app_def_t POMODOIST_APP;
void pomodoist_ui_init(void);
void pomodoist_ui_start(void);
bool pomodoist_countdown_critical(void);
void pomodoist_rail_init(lv_obj_t *clock);
void pomodoist_rail_update(lv_obj_t *clock);
