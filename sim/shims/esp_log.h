/* sim shim: esp_log.h — logs to stderr (unbuffered) so the last line before a
 * deadlock is never lost to stdio buffering. */
#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, ...) do { fprintf(stderr, "I (%s) ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGW(tag, ...) do { fprintf(stderr, "W (%s) ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGE(tag, ...) do { fprintf(stderr, "E (%s) ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGD(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, ...) do { (void)(tag); } while (0)
static inline void esp_log_level_set(const char *t, int l) { (void)t; (void)l; }
