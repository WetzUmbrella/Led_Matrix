# ESP-IDF component "proto" (T-B2-01): the shared wire layout. The tile build
# finds it through EXTRA_COMPONENT_DIRS in panel_sync/CMakeLists.txt; the Pico
# build adds proto.c to its own sources.
idf_component_register(SRCS "proto.c" "ack_outbox.c"
                       INCLUDE_DIRS ".")
