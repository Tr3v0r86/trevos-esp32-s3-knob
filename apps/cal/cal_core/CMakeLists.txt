# cal core: the calendar window model. Plain C99, no IDF headers, so it also builds on the
# host: `make test` here runs test_cal_model.c against IDF's bundled cJSON.
idf_component_register(SRCS "cal_model.c" "cal_google.c" "cal_glance.c"
                       INCLUDE_DIRS "."
                       REQUIRES json)
