CMAKE_MINIMUM_REQUIRED(VERSION 2.4)
PROJECT(MojoCrash)

IF(CMAKE_COMPILER_IS_GNUCC)
    ADD_DEFINITIONS(-Wall -ggdb3)
ENDIF(CMAKE_COMPILER_IS_GNUCC)

IF(MSVC)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS=1)
ENDIF(MSVC)

IF(APPLE)
    SET(OPTIONAL_LIBS ${OPTIONAL_LIBS} "-framework Carbon -framework SystemConfiguration")
ELSE(APPLE)
    FIND_PACKAGE(Threads)
    SET(OPTIONAL_LIBS ${OPTIONAL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
ENDIF(APPLE)

SET(MOJOCRASH_SRCS
    mojocrash.c
    mojocrash_report.c
    mojocrash_unix.c
    mojocrash_macosx.c
    mojocrash_linux.c
    mojocrash_windows.c
)

ADD_EXECUTABLE(test test.c ${MOJOCRASH_SRCS})
TARGET_LINK_LIBRARIES(test ${OPTIONAL_LIBS})

# End of CMakeLists.txt ...

