set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-field-initializers")

function(addPythonTest test_name)
    add_library(${test_name} SHARED ${test_name}.c)
    set_target_properties(${test_name} PROPERTIES PREFIX "")
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/python/cpython/Include/ ${CMAKE_SOURCE_DIR}/python/cpython ${CMAKE_SOURCE_DIR}/src/)
    target_link_libraries(${test_name}
        interp ${LLVM_LIB_DEPS}
    )
endfunction()

addPythonTest(pytest1)
addPythonTest(pytest2)
addPythonTest(pytest3)
addPythonTest(fib)
