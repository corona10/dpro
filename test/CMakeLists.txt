add_executable(test1 test1.c)
target_include_directories(test1 PRIVATE ${CMAKE_SOURCE_DIR}/src/)
target_link_libraries(test1
    interp ${LLVM_LIB_DEPS}
)

add_executable(test2 test2.c lib.c)
target_include_directories(test2 PRIVATE ${CMAKE_SOURCE_DIR}/src/)
target_link_libraries(test2
    interp ${LLVM_LIB_DEPS}
)
