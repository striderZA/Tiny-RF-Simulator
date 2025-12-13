
add_executable(unit_tests
    test_utils.cpp
    test_spectrum.cpp
)

target_link_libraries(unit_tests
    PRIVATE
        Catch2::Catch2WithMain
	spectrum_analyzer
        common
)

target_compile_features(unit_tests PRIVATE cxx_std_20)

include(CTest)
include(Catch)

catch_discover_tests(unit_tests)
