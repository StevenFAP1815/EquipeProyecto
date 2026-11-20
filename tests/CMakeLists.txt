find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    FetchContent_Declare(
        Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
        URL_HASH SHA256=269543a49eb76f40b3f93ff231d4c24c27a7e16c90e47d2e45bcc564de470c6e
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

add_executable(delivery_tests
    test_geobounds.cpp
    test_config_loader.cpp
    test_order_book.cpp
    test_shutdown.cpp
    test_generator_and_courier.cpp
    test_street_network.cpp
    test_dispatcher.cpp
    test_pickup_desk.cpp
    test_courier_bag.cpp
    test_breakdowns.cpp
    test_event_log.cpp
    test_device_info.cpp
    test_city_config.cpp
    test_bench_runner.cpp
    test_gpu_worker.cpp
    test_travel_table.cpp
    test_candidate_filter.cpp
    test_eta_batch.cpp
    test_argmin_reduction.cpp
    test_delivery_histogram.cpp
    test_radix_sort.cpp
    test_group_by_key.cpp
    test_segment_merge.cpp
    test_demand_heatmap.cpp
    test_coverage_field.cpp
    test_response_map.cpp
    test_route_arrivals.cpp
    test_gpu_thresholds.cpp
    test_frame_stats.cpp
    test_grid_binning.cpp
)

target_link_libraries(delivery_tests PRIVATE
    delivery_core
    Catch2::Catch2WithMain
)

target_compile_definitions(delivery_tests PRIVATE
    TESTS_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    CONFIG_DIR="${PROJECT_SOURCE_DIR}/config"
)

include(Catch)
catch_discover_tests(delivery_tests PROPERTIES TIMEOUT 30)
