include(QtSetup)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

taiga_add_version_definitions()

add_executable(taiga)

target_sources(taiga PRIVATE
	base/chrono.cpp
	base/chrono.hpp
	base/file.cpp
	base/file.hpp
	base/log.cpp
	base/log.hpp
	base/preprocessor.h
	base/rss.hpp
	base/settings.cpp
	base/settings.hpp
	base/string.cpp
	base/string.hpp
	base/xml.cpp
	base/xml.hpp

	compat/anime.cpp
	compat/anime.hpp
	compat/common.cpp
	compat/common.hpp
	compat/history.cpp
	compat/history.hpp
	compat/list.cpp
	compat/list.hpp
	compat/settings.cpp
	compat/settings.hpp

	media/anime_db.cpp
	media/anime_db.hpp
	media/anime_history.cpp
	media/anime_history.hpp
	media/anime_list_export.cpp
	media/anime_list_export.hpp
	media/anime_list_utils.cpp
	media/anime_list_utils.hpp
	media/anime_list.hpp
	media/anime_season.cpp
	media/anime_season.hpp
	media/anime_settings.hpp
	media/anime_utils.cpp
	media/anime_utils.hpp
	media/anime.hpp

	sync/anilist/anilist.cpp
	sync/anilist/anilist.hpp
	sync/anilist/anilist_auth.cpp
	sync/anilist/anilist_error.cpp
	sync/anilist/anilist_error.hpp
	sync/anilist/anilist_parsers.cpp
	sync/anilist/anilist_parsers.hpp
	sync/anilist/anilist_ratings.cpp
	sync/anilist/anilist_ratings.hpp
	sync/anilist/anilist_utils.cpp
	sync/anilist/anilist_utils.hpp
	sync/kitsu/kitsu.cpp
	sync/kitsu/kitsu.hpp
	sync/kitsu/kitsu_auth.cpp
	sync/kitsu/kitsu_error.cpp
	sync/kitsu/kitsu_error.hpp
	sync/kitsu/kitsu_parsers.cpp
	sync/kitsu/kitsu_parsers.hpp
	sync/kitsu/kitsu_ratings.cpp
	sync/kitsu/kitsu_ratings.hpp
	sync/kitsu/kitsu_utils.cpp
	sync/kitsu/kitsu_utils.hpp
	sync/myanimelist/myanimelist.cpp
	sync/myanimelist/myanimelist.hpp
	sync/myanimelist/myanimelist_auth.cpp
	sync/myanimelist/myanimelist_error.cpp
	sync/myanimelist/myanimelist_error.hpp
	sync/myanimelist/myanimelist_parsers.cpp
	sync/myanimelist/myanimelist_parsers.hpp
	sync/myanimelist/myanimelist_ratings.cpp
	sync/myanimelist/myanimelist_ratings.hpp
	sync/myanimelist/myanimelist_utils.cpp
	sync/myanimelist/myanimelist_utils.hpp
	sync/queue.cpp
	sync/queue.hpp
	sync/service.cpp
	sync/service.hpp

	taiga/accounts.cpp
	taiga/accounts.hpp
	taiga/application.cpp
	taiga/application.hpp
	taiga/config.h
	taiga/network.cpp
	taiga/network.hpp
	taiga/orange.cpp
	taiga/orange.hpp
	taiga/path.cpp
	taiga/path.hpp
	taiga/session.cpp
	taiga/session.hpp
	taiga/settings.cpp
	taiga/settings.hpp
	taiga/version.cpp
	taiga/version.hpp

	track/episode.cpp
	track/episode.hpp
	track/media.cpp
	track/media.hpp
	track/media_player.cpp
	track/media_player.hpp
	track/play.cpp
	track/play.hpp
	track/recognition.cpp
	track/recognition.hpp
	track/recognition_cache.cpp
	track/recognition_cache.hpp
	track/recognition_normalize.cpp
	track/recognition_normalize.hpp
	track/recognition_path.cpp
	track/recognition_path.hpp
	track/recognition_relations.cpp
	track/recognition_relations.hpp
	track/recognition_validate.cpp
	track/recognition_validate.hpp
	track/scanner.cpp
	track/scanner.hpp
	track/sharing.cpp
	track/sharing.hpp
	track/stream.cpp
	track/stream.hpp
	track/seadex.cpp
	track/seadex.hpp
	track/torrent_feed.cpp
	track/torrent_feed.hpp
	track/torrent_download.cpp
	track/torrent_download.hpp
	track/torrent_settings.cpp
	track/torrent_settings.hpp

	main.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_sources(taiga PRIVATE
		track/mpris.cpp
		track/mpris.hpp
	)
endif()

target_link_libraries(taiga PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Network
	Qt6::Sql
	Qt6::Widgets
	taiga-config
	taiga-deps
	taiga-gui
	taiga-resources
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(taiga PRIVATE Qt6::DBus)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TAIGA_BUILD_MPRIS_PROBE)
	add_executable(taiga-mpris-probe
		${CMAKE_SOURCE_DIR}/tests/mpris_probe.cpp
		track/mpris.cpp
		track/mpris.hpp
	)
	target_include_directories(taiga-mpris-probe PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_SOURCE_DIR}/deps/anisthesia/include
	)
	target_link_libraries(taiga-mpris-probe PRIVATE
		Qt6::Core
		Qt6::DBus
	)
endif()

if (TAIGA_PORTABLE)
	target_compile_definitions(taiga PRIVATE TAIGA_PORTABLE)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set_target_properties(taiga PROPERTIES OUTPUT_NAME Taiga)
	set_target_properties(taiga PROPERTIES WIN32_EXECUTABLE ON)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	install(TARGETS taiga
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)
	install(FILES resources/taiga.desktop
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
	)
	install(FILES resources/moe.taiga.Taiga.metainfo.xml
		DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
	)
	install(FILES resources/icons/taiga.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
	)
endif()

add_subdirectory(gui)
add_subdirectory(resources)

if (TAIGA_ENABLE_PCH)
	# Keep separate PCHs: the application and GUI have different compile definitions.
	foreach(target IN ITEMS taiga taiga-gui)
		target_precompile_headers(${target} PRIVATE
			<algorithm>
			<optional>
			<ranges>
			<string>
			<vector>
			<QList>
			<QObject>
			<QString>
			<QJsonObject>
			<QUrl>
		)
	endforeach()
	target_precompile_headers(taiga-gui PRIVATE <QWidget>)
endif()

if (TAIGA_BUILD_TORRENT_TESTS)
	add_executable(taiga-stream-test
		${CMAKE_SOURCE_DIR}/tests/stream_test.cpp
		track/stream.cpp
		track/stream.hpp
	)
	target_link_libraries(taiga-stream-test PRIVATE Qt6::Core)
	add_test(NAME stream COMMAND taiga-stream-test)

	add_executable(taiga-torrent-feed-test
		${CMAKE_SOURCE_DIR}/tests/torrent_feed_test.cpp
		track/torrent_feed.cpp
		track/torrent_feed.hpp
		track/seadex.cpp
		track/seadex.hpp
	)
	target_link_libraries(taiga-torrent-feed-test PRIVATE Qt6::Core Qt6::Network)
	add_test(NAME torrent-feed COMMAND taiga-torrent-feed-test)

	add_executable(taiga-torrent-download-test
		${CMAKE_SOURCE_DIR}/tests/torrent_download_test.cpp
		track/torrent_feed.cpp
		track/torrent_feed.hpp
		track/torrent_download.cpp
		track/torrent_download.hpp
		track/torrent_settings.cpp
		track/torrent_settings.hpp
	)
	target_link_libraries(taiga-torrent-download-test PRIVATE Qt6::Core Qt6::Network)
	add_test(NAME torrent-download COMMAND taiga-torrent-download-test)

	add_executable(taiga-torrent-widget-test
		${CMAKE_SOURCE_DIR}/tests/torrent_widget_test.cpp
		gui/common/page_widget.cpp
		gui/common/page_widget.hpp
		gui/torrents/torrents_widget.cpp
		gui/torrents/torrents_widget.hpp
		track/torrent_feed.cpp
		track/torrent_feed.hpp
		track/seadex.cpp
		track/seadex.hpp
		track/torrent_download.cpp
		track/torrent_download.hpp
		track/torrent_settings.cpp
		track/torrent_settings.hpp
	)
	target_link_libraries(taiga-torrent-widget-test PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)
	add_test(NAME torrent-widget COMMAND taiga-torrent-widget-test)
	set_tests_properties(torrent-widget PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion"
	)
endif()

if (TAIGA_ENABLE_TRANSLATIONS)
	qt_add_translations(taiga
		SOURCE_TARGETS taiga-gui
		TS_FILE_BASE taiga
		TS_FILE_DIR resources/translations
		LUPDATE_OPTIONS -no-obsolete -locations none
	)
endif()

if (TAIGA_BUILD_RECOGNITION_TESTS)
	add_executable(taiga-recognition-test
		${CMAKE_SOURCE_DIR}/tests/recognition_test.cpp
		media/anime_db.hpp
		base/chrono.cpp
		base/string.cpp
		track/episode.cpp
		track/recognition.cpp
		track/recognition_cache.cpp
		track/recognition_normalize.cpp
		track/recognition_path.cpp
		track/recognition_validate.cpp
		track/stream.cpp
	)
	target_link_libraries(taiga-recognition-test PRIVATE Qt6::Core Qt6::Gui Qt6::Sql taiga-deps)
	add_test(NAME recognition COMMAND taiga-recognition-test)
endif()
