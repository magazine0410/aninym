cmake_minimum_required(VERSION 3.21)

project(aninym
	VERSION 0.1.0
	DESCRIPTION "Visual novel detection library for Linux"
	LANGUAGES CXX
)

option(ANINYM_X11 "Read window titles from X11 (needs libxcb)" ON)
option(ANINYM_BUILD_TESTS "Build aninym's tests" ${PROJECT_IS_TOP_LEVEL})
option(ANINYM_BUILD_TOOLS "Build the aninym-scan tool" ${PROJECT_IS_TOP_LEVEL})

add_library(aninym STATIC
	src/game.cpp
	src/matcher.cpp
	src/steam.cpp
	src/title.cpp
	src/utf8.cpp
	src/wine.cpp
	src/x11.cpp
)
add_library(aninym::aninym ALIAS aninym)

target_include_directories(aninym PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(aninym PUBLIC cxx_std_20)
set_target_properties(aninym PROPERTIES
	CXX_EXTENSIONS OFF
	POSITION_INDEPENDENT_CODE ON
)

if (PROJECT_IS_TOP_LEVEL AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(aninym PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Wine and Proton games draw through X11 (Xwayland, under a Wayland session), so
# that is where their window titles are. Without libxcb aninym still finds the
# games, just not their titles.
if (ANINYM_X11)
	find_package(PkgConfig QUIET)
	if (PkgConfig_FOUND)
		# GLOBAL, so that a project linking aninym from another directory can see
		# the imported target that aninym's link interface names.
		pkg_check_modules(ANINYM_XCB QUIET IMPORTED_TARGET GLOBAL xcb)
	endif()
	if (ANINYM_XCB_FOUND)
		target_link_libraries(aninym PRIVATE PkgConfig::ANINYM_XCB)
		target_compile_definitions(aninym PRIVATE ANINYM_HAVE_XCB)
	else()
		message(WARNING "aninym: libxcb was not found, so window titles will not be read.")
	endif()
endif()

if (ANINYM_BUILD_TESTS)
	enable_testing()
	add_executable(aninym-tests
		test/main.cpp
		test/game_test.cpp
		test/matcher_test.cpp
		test/title_test.cpp
		test/wine_test.cpp
	)
	target_link_libraries(aninym-tests PRIVATE aninym)
	set_target_properties(aninym-tests PROPERTIES CXX_EXTENSIONS OFF)
	add_test(NAME aninym COMMAND aninym-tests)
endif()

if (ANINYM_BUILD_TOOLS)
	add_executable(aninym-scan tools/aninym_scan.cpp)
	target_link_libraries(aninym-scan PRIVATE aninym)
	set_target_properties(aninym-scan PROPERTIES CXX_EXTENSIONS OFF)
endif()
