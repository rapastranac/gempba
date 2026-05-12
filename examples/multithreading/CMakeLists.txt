file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

foreach (EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    set(TARGET_NAME multithreading_${EXAMPLE_NAME})

    add_executable(${TARGET_NAME} ${EXAMPLE_SOURCE})

    target_include_directories(${TARGET_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${Boost_INCLUDE_DIRS}
            ${ARGPARSE_INCLUDE_DIR}
    )

    target_link_libraries(${TARGET_NAME} PRIVATE
            gempba
            example_libs
            Boost::serialization
            Boost::fiber
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )

    target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Wpedantic
            $<$<CONFIG:Debug>:-O0 -ggdb3>
    )

    target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:GEMPBA_DEBUG_COMMENTS>
    )
endforeach ()
