set(SOURCES
	e-etesync-connection.c
	e-etesync-connection.h
	e-source-etesync.c
	e-source-etesync.h
	e-etesync-defines.h
)

add_library(evolution-etesync SHARED
	${SOURCES}
)

target_compile_definitions(evolution-etesync PRIVATE
	-DG_LOG_DOMAIN=\"evolution-etesync\"
)

target_compile_options(evolution-etesync PUBLIC
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${ETESYNC_CFLAGS}
)

target_include_directories(evolution-etesync PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${EVOLUTION_CALENDAR_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${ETESYNC_INCLUDE_DIRS}
)

target_link_libraries(evolution-etesync
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${ETESYNC_LDFLAGS}
)

install(TARGETS evolution-etesync
	DESTINATION ${privsolibdir}
)
