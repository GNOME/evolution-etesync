set(DEPENDENCIES
	evolution-etesync
)

set(SOURCES
	e-cal-backend-etesync-factory.c
	e-cal-backend-etesync.c
	e-cal-backend-etesync.h
)

add_library(ecalbackendetesync MODULE
	${SOURCES}
)

add_dependencies(ecalbackendetesync
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendetesync PRIVATE
	-DG_LOG_DOMAIN=\"ecalbackendetesync\"
	-DETESYNC_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DEXCHANGE_ETESYNC_SRCDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_compile_options(ecalbackendetesync PUBLIC
	${CAMEL_CFLAGS}
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${CALENDAR_CFLAGS}
	${ETESYNC_CFLAGS}
)

target_include_directories(ecalbackendetesync PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${EVOLUTION_CALENDAR_INCLUDE_DIRS}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${CALENDAR_INCLUDE_DIRS}
	${ETESYNC_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendetesync
	${DEPENDENCIES}
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${CALENDAR_LDFLAGS}
	${ETESYNC_LDFLAGS}
)

install(TARGETS ecalbackendetesync
	DESTINATION ${ecal_backenddir}
)
