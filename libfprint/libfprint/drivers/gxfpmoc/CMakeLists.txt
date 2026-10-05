cmake_minimum_required(VERSION 3.16)

project(gxfpmoc C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(MbedTLS REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)

add_library(gxfp
		src/io/dev.c
		src/algo/common.c
		src/algo/sensor_cfg.c
		src/algo/image/plain.c
		src/algo/image/decoder.c
		src/algo/image/fpn.c
		src/algo/image/chicago_backend.c
		src/algo/image/chicago_native.c
		src/algo/payload/provision.c
		src/proto/goodix_proto.c
		src/tls/tls_engine_mbedtls.c
		src/tls/tls_service.c
		src/flow/device_recovery.c
		src/flow/fdt.c
		src/flow/session.c
		src/cmd/tls_cmd.c
		src/cmd/goodix_xfer.c
		src/cmd/production_cmd.c
		src/cmd/device_recovery_cmd.c
		src/cmd/fdt_cmd.c
		src/cmd/mcu_state_cmd.c
		src/cmd/sensor_cfg_cmd.c
		src/cmd/register_cmd.c
)

# Standalone native stages for synthetic conformance and the gradual port.
add_library(gxfp_chicago_native SHARED
	src/algo/image/chicago_native.c
	src/algo/image/chicago_backend.c)
target_include_directories(gxfp_chicago_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(gxfp_chicago_native PRIVATE -Wall -Wextra -Wpedantic)
# close_range() and friends need the same feature macro as the driver library.
target_compile_definitions(gxfp_chicago_native PRIVATE _GNU_SOURCE)

target_include_directories(gxfp
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(gxfp PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(gxfp PRIVATE _GNU_SOURCE)
target_link_libraries(gxfp PUBLIC m)

target_link_libraries(gxfp PUBLIC MbedTLS::mbedtls)

# Chicago matcher, ported verbatim from libfprint-goodix-spi (LGPL-2.1-or-later).
# See src/algo/match/PORTING.md: sources are byte-identical to upstream apart from one
# include path, and the upstream test suite is carried in tests/matcher/.
add_library(gxfp_chicago_matcher STATIC
	src/algo/match/goodix-chicago-bir.c
	src/algo/match/goodix-chicago-calibration.c
	src/algo/match/goodix-chicago-enrollment.c
	src/algo/match/goodix-chicago-feature.c
	src/algo/match/goodix-chicago-late-rejection.c
	src/algo/match/goodix-chicago-late-rejection-compat.c
	src/algo/match/goodix-chicago-match.c
	src/algo/match/goodix-chicago-preprocess.c
	src/algo/match/goodix-chicago-runtime.c
	src/algo/match/goodix-chicago-template.c
	src/algo/match/goodix-crc.c)
target_include_directories(gxfp_chicago_matcher PUBLIC
	include/gxfp/algo/match
	src/algo/match)
target_link_libraries(gxfp_chicago_matcher PUBLIC PkgConfig::GLIB2 m)

if(BUILD_TESTING)
    # End-to-end acceptance test for the ported recognition chain: calibration,
    # preprocessing, feature extraction, template pack/unpack, geometric matching
    # with late rejection, print data and adaptive study, in one process.
    add_executable(gxfp_chicago_chain_test tests/matcher/chicago_chain_test.c)
    target_compile_options(gxfp_chicago_chain_test PRIVATE -UNDEBUG)
    target_link_libraries(gxfp_chicago_chain_test PRIVATE gxfp_chicago_matcher)
    add_test(NAME gxfp_chicago_chain COMMAND gxfp_chicago_chain_test)

    # Upstream matcher suite, ported verbatim. The oracle-vector cases skip unless the
    # CHICAGO_*_VECTOR / CHICAGO_*_EXPECTED environment variables point at vector files
    # produced from the factory DLL.
    foreach(matcher_test bir calibration enrollment feature match preprocess template)
        set(matcher_src tests/matcher/test-goodix-chicago-${matcher_test}.c)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${matcher_src})
            add_executable(gxfp_matcher_${matcher_test} ${matcher_src})
            target_compile_options(gxfp_matcher_${matcher_test} PRIVATE -UNDEBUG)
            target_link_libraries(gxfp_matcher_${matcher_test} PRIVATE gxfp_chicago_matcher)
            add_test(NAME gxfp_matcher_${matcher_test} COMMAND gxfp_matcher_${matcher_test})
        endif()
    endforeach()

    add_executable(gxfp_chicago_native_test tests/chicago_native_test.c)
    target_compile_options(gxfp_chicago_native_test PRIVATE -UNDEBUG)
    target_link_libraries(gxfp_chicago_native_test PRIVATE gxfp)
    add_test(NAME gxfp_chicago_native COMMAND gxfp_chicago_native_test)
    add_executable(gxfp_chicago_test tests/chicago_test.c)
    target_compile_options(gxfp_chicago_test PRIVATE -UNDEBUG)
    target_link_libraries(gxfp_chicago_test PRIVATE gxfp)
    add_test(NAME gxfp_chicago COMMAND gxfp_chicago_test)
	add_executable(gxfp_fdt_retry_test tests/fdt_retry_test.c)
	target_compile_options(gxfp_fdt_retry_test PRIVATE -UNDEBUG)
	target_link_libraries(gxfp_fdt_retry_test PRIVATE gxfp)
	add_test(NAME gxfp_fdt_retry_policy COMMAND gxfp_fdt_retry_test)
endif()
