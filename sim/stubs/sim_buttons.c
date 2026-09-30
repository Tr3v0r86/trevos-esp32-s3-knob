/* sim/stubs/sim_buttons.c — host button input on a SEPARATE thread.
 *
 * This is the thread that makes the focus-freeze reproducible: the device fires on_btn
 * from btn_task (a FreeRTOS task distinct from the LVGL task), and on_btn takes the
 * lvgl_port lock cross-thread. So here a pthread reads commands and calls the registered
 * cb from that thread — never from the LVGL loop.
 *
 * Commands (one per line on stdin): KEY | KEY_LONG | BOOT | BOOT_LONG | QUIT
 * (papersim adds C | C_LONG for its third button.)
 * Scriptable:  printf 'KEY\nBOOT\n' | ./t3sim
 *
 * The "fire"/"returned" log bracket is the freeze detector: if "returned" never prints
 * after a command, on_btn deadlocked (the whole point of issue #3).
 */
#include "bsp_buttons.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bsp_btn_cb_t s_cb;
static void        *s_ctx;

#ifdef BSP_BTN3
#define BTN_USAGE "KEY|KEY_LONG|BOOT|BOOT_LONG|C|C_LONG|QUIT"
#else
#define BTN_USAGE "KEY|KEY_LONG|BOOT|BOOT_LONG|QUIT"
#endif

static int parse(const char *s, bsp_btn_t *b, bsp_btn_evt_t *e)
{
    if      (!strcmp(s, "KEY"))       { *b = BSP_BTN_KEY;  *e = BSP_BTN_PRESS; }
    else if (!strcmp(s, "KEY_LONG"))  { *b = BSP_BTN_KEY;  *e = BSP_BTN_LONG;  }
    else if (!strcmp(s, "BOOT"))      { *b = BSP_BTN_BOOT; *e = BSP_BTN_PRESS; }
    else if (!strcmp(s, "BOOT_LONG")) { *b = BSP_BTN_BOOT; *e = BSP_BTN_LONG;  }
#ifdef BSP_BTN3
    else if (!strcmp(s, "C"))         { *b = BSP_BTN_C;    *e = BSP_BTN_PRESS; }
    else if (!strcmp(s, "C_LONG"))    { *b = BSP_BTN_C;    *e = BSP_BTN_LONG;  }
#endif
    else return 0;
    return 1;
}

static void *btn_thread(void *arg)
{
    (void)arg;
    char line[64];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 0) continue;
        if (!strcmp(line, "QUIT")) { fprintf(stderr, "[btn] QUIT\n"); exit(0); }
        bsp_btn_t b; bsp_btn_evt_t e;
        if (!parse(line, &b, &e)) { fprintf(stderr, "[btn] ? '%s' (" BTN_USAGE ")\n", line); continue; }
        fprintf(stderr, "[btn] fire %s\n", line);
        if (s_cb) s_cb(b, e, s_ctx);
        fprintf(stderr, "[btn] returned %s\n", line);   /* absent => on_btn deadlocked */
    }
    return NULL;
}

void bsp_buttons_init(bsp_btn_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
#ifdef CYDSIM
    /* The CYD is touch-only (no app buttons), and the touch stub (sim_touch) owns
     * stdin. Spawning the button reader here would race it for input lines. */
    (void)btn_thread;
    fprintf(stderr, "[btn] disabled under cydsim (touch owns stdin)\n");
#else
    pthread_t t;
    pthread_create(&t, NULL, btn_thread, NULL);
    pthread_detach(t);
    fprintf(stderr, "[btn] input thread up (stdin: " BTN_USAGE ")\n");
#endif
}
