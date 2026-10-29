add_executable(unit_tests
    test_main.cpp
    test_log.cpp
    test_core.cpp
    test_games.cpp
    test_fm6_font.cpp
    test_stock_wheels.cpp
    test_options.cpp
    test_real_exes.cpp
    test_launcher.cpp
    image.cpp
    test_proxy.cpp
    test_md2txt.cpp
)
target_include_directories(unit_tests SYSTEM PRIVATE doctest)
# the test runner is not shipped: doctest REQUIRE needs exceptions
target_compile_options(unit_tests PRIVATE -fexceptions)
target_link_libraries(unit_tests PRIVATE games launcher md2txt_lib)
target_compile_definitions(unit_tests PRIVATE
    TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    TEST_OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_WINMM_DLL="$<TARGET_FILE:winmm>"
)
add_dependencies(unit_tests winmm)
add_test(NAME unit_tests COMMAND unit_tests)

# winmm.dll may only import system DLLs (it ships alone, without the mingw runtime)
add_test(NAME winmm_imports
    COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${CMAKE_OBJDUMP} -DDLL=$<TARGET_FILE:winmm>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_imports.cmake)

# library isolation: no source reaches another folder with a relative include
add_test(NAME include_isolation
    COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR}/src -P ${CMAKE_CURRENT_SOURCE_DIR}/check_includes.cmake)
