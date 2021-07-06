cmake_minimum_required(VERSION 3.0.0)
project(MojoCrash)

if(CMAKE_COMPILER_IS_GNUCC)
    add_definitions(-Wall -ggdb3)
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS=1)
endif()

if(APPLE)
    set(OPTIONAL_LIBS ${OPTIONAL_LIBS} "-framework Carbon -framework SystemConfiguration")
else()
    find_package(Threads)
    SET(OPTIONAL_LIBS ${OPTIONAL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

SET(MOJOCRASH_SRCS
    mojocrash.c
    mojocrash_report.c
    mojocrash_unix.c
    mojocrash_macosx.c
    mojocrash_linux.c
    mojocrash_windows.c
)

add_executable(test test.c ${MOJOCRASH_SRCS})
target_link_libraries(test ${OPTIONAL_LIBS})

# End of CMakeLists.txt ...

