cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBMPG123_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/reframe_mpg123.c
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_mpg123.c
)

SET(LIBMPG123_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmpg123.a
)

SET(LIBMPG123_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libmpg123
        "${LIBMPG123_SRC}"
        "${LIBMPG123_LIB}"
        []
        ""
        "${LIBMPG123_INC}"
        ""
        "1")
