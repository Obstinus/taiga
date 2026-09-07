cmake_minimum_required(VERSION 3.21)

# Allow -DTAIGA_VERSION=major.minor[.patch][-prerelease] or TAIGA_VERSION in the environment.
include(cmake/TaigaVersion.cmake)

project(Taiga
	VERSION ${TAIGA_VERSION_NUMERIC}
	DESCRIPTION "The desktop tiger"
	HOMEPAGE_URL "https://taiga.moe/"
	LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

include(GNUInstallDirs)

option(TAIGA_PORTABLE "Portable mode" ON)
option(TAIGA_ENABLE_TRANSLATIONS "Build Qt translation files" ON)
option(TAIGA_BUILD_MPRIS_PROBE "Build the Linux MPRIS integration probe" OFF)
option(TAIGA_BUILD_TORRENT_TESTS "Build isolated torrent integration tests" OFF)

if (TAIGA_BUILD_TORRENT_TESTS)
	enable_testing()
endif()

include(TaigaConfig)

# utf8proc is linked statically into Taiga. Do not copy its development files
# into the application package as a side effect of its vendored install rules.
set(UTF8PROC_INSTALL OFF CACHE BOOL "Install utf8proc development files" FORCE)

add_subdirectory(deps)
add_subdirectory(src)

set(CPACK_PACKAGE_NAME "taiga")
set(CPACK_PACKAGE_VENDOR "Taiga contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Anime library and progress tracker")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CONTACT "taiga@erengy.com")
set(CPACK_PACKAGE_VERSION "${TAIGA_VERSION_STRING}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Taiga")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(CPACK_GENERATOR "TGZ;DEB")
	set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Taiga contributors")
	set(CPACK_DEBIAN_PACKAGE_VERSION "${TAIGA_VERSION_DEBIAN}")
	set(CPACK_DEBIAN_PACKAGE_SECTION "video")
	set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CMAKE_PROJECT_HOMEPAGE_URL}")
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
