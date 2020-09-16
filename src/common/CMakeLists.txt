set(SOURCES
	e-etesync-connection.c
	e-etesync-connection.h
	e-source-etesync.c
	e-source-etesync.h
	e-source-etesync-account.c
	e-source-etesync-account.h
	e-etesync-service.c
	e-etesync-service.h
	e-etesync-utils.c
	e-etesync-utils.h
	e-etesync-defines.h
)

add_library(evolution-etesync SHARED
	${SOURCES}
)

target_compile_definitions(evolution-etesync PRIVATE
	-DG_LOG_DOMAIN=\"evolution-etesync\"
)

target_compile_options(evolution-etesync PUBLIC
	${ETESYNC_CFLAGS}
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEBOOK_CFLAGS}
	${LIBEDATABOOK_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
)

target_include_directories(evolution-etesync PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${ETESYNC_INCLUDE_DIRS}
	${EVOLUTION_CALENDAR_INCLUDE_DIRS}
	${LIBEBOOK_INCLUDE_DIRS}
	${LIBEDATABOOK_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
)

target_link_libraries(evolution-etesync
	${ETESYNC_LDFLAGS}
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEBOOK_LDFLAGS}
	${LIBEDATABOOK_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
)

install(TARGETS evolution-etesync
	DESTINATION ${privsolibdir}
)
