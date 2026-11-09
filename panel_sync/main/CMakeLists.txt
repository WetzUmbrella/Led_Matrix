idf_component_register(SRCS "panel_sync.c" "timesync.c" "tile_net.c" "display_engine.c"
                         "fault_word.c" "tile_tasks.c" "asset_store.c" "playback_control.c" "telemetry_book.c" "asset_rx.c" "coord_lock.c" "display_frame.c" "game_tile.c" "orient_filter.c" "tap_detect.c" "orientation.c" "tile_stress.c"
                    INCLUDE_DIRS ".")

# OD-09 receive hook for tile_net (T-B3-03): idf.py -DTN_RX_HOOK=2 build
# selects recvfrom in the Network Task; the default is the lwIP callback.
if(DEFINED TN_RX_HOOK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TN_RX_HOOK=${TN_RX_HOOK})
endif()
