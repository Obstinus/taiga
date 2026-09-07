# Keep the numeric CMake project version separate from the release version.
set(DEFAULT_VERSION "2.0.1")

if(NOT TAIGA_VERSION AND DEFINED ENV{TAIGA_VERSION})
	set(TAIGA_VERSION "$ENV{TAIGA_VERSION}")
endif()

set(_taiga_version "${TAIGA_VERSION}")
if(NOT _taiga_version AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
	find_package(Git QUIET)
	if(GIT_FOUND)
		# Read the tag itself: describe's commit-distance suffix is not a prerelease.
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --abbrev=0
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			OUTPUT_VARIABLE _taiga_version
			OUTPUT_STRIP_TRAILING_WHITESPACE
			RESULT_VARIABLE _taiga_git_result
			ERROR_QUIET
		)
		if(NOT _taiga_git_result EQUAL 0)
			set(_taiga_version "")
		endif()
	endif()
endif()

if(NOT _taiga_version)
	set(_taiga_version "${DEFAULT_VERSION}")
endif()

# Accept major.minor as shorthand for major.minor.0, including prereleases.
if(NOT _taiga_version MATCHES "^v?([0-9]+)\\.([0-9]+)(\\.([0-9]+))?(-([0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*))?$")
	message(FATAL_ERROR "Invalid Taiga version '${_taiga_version}': expected major.minor[.patch][-prerelease]")
endif()
set(_taiga_major "${CMAKE_MATCH_1}")
set(_taiga_minor "${CMAKE_MATCH_2}")
set(_taiga_patch "${CMAKE_MATCH_4}")
set(TAIGA_VERSION_PRERELEASE "${CMAKE_MATCH_6}")
if(_taiga_patch STREQUAL "")
	set(_taiga_patch 0)
endif()

# Leading zeroes are invalid SemVer and would produce octal C++ literals.
foreach(_taiga_component IN ITEMS _taiga_major _taiga_minor _taiga_patch)
	if(${_taiga_component} MATCHES "^0[0-9]+$")
		message(FATAL_ERROR "Invalid Taiga version '${_taiga_version}': leading zero in numeric component")
	endif()
endforeach()

set(TAIGA_VERSION_NUMERIC "${_taiga_major}.${_taiga_minor}.${_taiga_patch}")
set(TAIGA_VERSION_STRING "${TAIGA_VERSION_NUMERIC}")
set(TAIGA_VERSION_DEBIAN "${TAIGA_VERSION_NUMERIC}")
if(NOT TAIGA_VERSION_PRERELEASE STREQUAL "")
	string(APPEND TAIGA_VERSION_STRING "-${TAIGA_VERSION_PRERELEASE}")
	# Debian's tilde sorts before the corresponding final release.
	string(APPEND TAIGA_VERSION_DEBIAN "~${TAIGA_VERSION_PRERELEASE}")
endif()

function(taiga_add_version_definitions)
	add_compile_definitions(
		TAIGA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
		TAIGA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
		TAIGA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
		TAIGA_VERSION_PRE="${TAIGA_VERSION_PRERELEASE}"
		TAIGA_VERSION_BUILD=0
	)
endfunction()
