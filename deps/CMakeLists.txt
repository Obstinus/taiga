add_library(taiga-deps STATIC)

target_link_libraries(taiga-deps
	anisthesia
	anitomy
	nstd
	semaver
	taiga-config
	utf8proc
)

add_subdirectory(anisthesia)
# Taiga needs the parser library, not the standalone CLI and upstream tests.
# Those targets remain available when explicitly requested.
add_subdirectory(anitomy EXCLUDE_FROM_ALL)
add_subdirectory(nstd)
add_subdirectory(semaver)
add_subdirectory(utf8proc)
