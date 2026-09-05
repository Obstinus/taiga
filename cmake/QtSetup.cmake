set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

list(APPEND CMAKE_PREFIX_PATH "%QTDIR%/lib/cmake")

set(TAIGA_QT_COMPONENTS
	Concurrent
	Core
	Gui
	Network
	Sql
	Svg
	Widgets
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND TAIGA_QT_COMPONENTS DBus)
endif()

if (TAIGA_ENABLE_TRANSLATIONS)
	list(APPEND TAIGA_QT_COMPONENTS LinguistTools)
endif()

find_package(Qt6 REQUIRED COMPONENTS ${TAIGA_QT_COMPONENTS})

qt_standard_project_setup(
	REQUIRES 6.8
)
