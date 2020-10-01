set(DEPENDENCIES
	evolution-etesync
)

set(SOURCES
	e-book-backend-etesync.c
	e-book-backend-etesync.h
	e-book-backend-etesync-factory.c
)

add_library(ebookbackendetesync MODULE
	${SOURCES}
)

add_dependencies(ebookbackendetesync
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendetesync PRIVATE
	-DG_LOG_DOMAIN=\"ebookbackendetesync\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
	-DETESYNC_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(ebookbackendetesync PUBLIC
	${LIBEBACKEND_CFLAGS}
	${LIBEBOOK_CFLAGS}
	${LIBEDATABOOK_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${ADDRESSBOOK_CFLAGS}
	${ETESYNC_CFLAGS}
)

target_include_directories(ebookbackendetesync PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBEBOOK_INCLUDE_DIRS}
	${LIBEDATABOOK_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${ADDRESSBOOK_INCLUDE_DIRS}
	${ETESYNC_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendetesync
	${DEPENDENCIES}
	${LIBEBACKEND_LDFLAGS}
	${LIBEBOOK_LDFLAGS}
	${LIBEDATABOOK_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${ADDRESSBOOK_LDFLAGS}
	${ETESYNC_LDFLAGS}
)

install(TARGETS ebookbackendetesync
	DESTINATION ${ebook_backenddir}
)
