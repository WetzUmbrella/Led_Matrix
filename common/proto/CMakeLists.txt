# ESP-IDF component "proto" (T-B2-01): the shared wire layout. The tile build
# finds it through EXTRA_COMPONENT_DIRS in panel_sync/CMakeLists.txt; the Pico
# build adds proto.c to its own sources.
# Minimal implementation by Buddy 4 for T-B4-03. Owner: Benjamin, task T-B2-01.
idf_component_register(SRCS "proto.c" "ack_outbox.c"
                       INCLUDE_DIRS ".")
