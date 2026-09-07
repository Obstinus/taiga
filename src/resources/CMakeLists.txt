add_library(taiga-resources OBJECT)

# Link the resource objects directly so their initializers are retained, and
# avoid propagating the qrc sources to the executable for a second compilation.
target_sources(taiga-resources PRIVATE
	deps.qrc
	gql.qrc
	icons.qrc
	styles.qrc
	sql.qrc
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	target_sources(taiga PRIVATE
		taiga.manifest
		taiga.rc
	)
endif()

target_link_libraries(taiga-resources
	PRIVATE Qt6::Core
	PUBLIC taiga-config
)
