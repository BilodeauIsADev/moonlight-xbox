cmake_minimum_required(VERSION 3.16)
project(moonlight_xbox_tests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_executable(pc_pin_tests PcPinTests.cpp)
target_compile_features(pc_pin_tests PRIVATE cxx_std_17)
add_test(NAME pc_pin COMMAND pc_pin_tests)

add_executable(wake_on_lan_tests WakeOnLanTests.cpp)
target_compile_features(wake_on_lan_tests PRIVATE cxx_std_17)
add_test(NAME wake_on_lan COMMAND wake_on_lan_tests)

add_executable(orbit_wake_tests OrbitWakeTests.cpp)
target_compile_features(orbit_wake_tests PRIVATE cxx_std_17)
add_test(NAME orbit_wake COMMAND orbit_wake_tests)

add_executable(orbit_navigation_tests OrbitNavigationTests.cpp)
target_compile_features(orbit_navigation_tests PRIVATE cxx_std_17)
add_test(NAME orbit_navigation COMMAND orbit_navigation_tests)

add_executable(orbit_recents_tests OrbitRecentsTests.cpp)
target_compile_features(orbit_recents_tests PRIVATE cxx_std_17)
add_test(NAME orbit_recents COMMAND orbit_recents_tests)

add_executable(orbit_stream_ui_tests OrbitStreamUiTests.cpp)
target_compile_features(orbit_stream_ui_tests PRIVATE cxx_std_17)
add_test(NAME orbit_stream_ui COMMAND orbit_stream_ui_tests)

add_executable(orbit_artwork_tests OrbitArtworkTests.cpp)
target_compile_features(orbit_artwork_tests PRIVATE cxx_std_17)
add_test(NAME orbit_artwork COMMAND orbit_artwork_tests)

add_executable(orbit_host_card_tests OrbitHostCardTests.cpp)
target_compile_features(orbit_host_card_tests PRIVATE cxx_std_17)
add_test(NAME orbit_host_card COMMAND orbit_host_card_tests)

add_executable(stream_session_tests StreamSessionTests.cpp)
target_compile_features(stream_session_tests PRIVATE cxx_std_17)
target_link_libraries(stream_session_tests PRIVATE Threads::Threads)
add_test(NAME stream_session COMMAND stream_session_tests)
set_tests_properties(stream_session PROPERTIES TIMEOUT 15)

set(PYROWAVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/pyrowave")
if(NOT EXISTS "${PYROWAVE_ROOT}/metal/pyrowave_bitstream.cpp")
    message(FATAL_ERROR "Initialize submodules with git submodule update --init --recursive")
endif()

add_executable(pyrowave_framing_tests
    PyroWaveFramingTests.cpp
    ../Streaming/PyroWave/PyroWaveFraming.cpp
    "${PYROWAVE_ROOT}/metal/pyrowave_bitstream.cpp"
)
target_compile_features(pyrowave_framing_tests PRIVATE cxx_std_17)
target_include_directories(pyrowave_framing_tests PRIVATE
    ../Streaming/PyroWave
    "${PYROWAVE_ROOT}/metal"
)
add_test(NAME pyrowave_framing COMMAND pyrowave_framing_tests)
