set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ferror-limit=5 -fcolor-diagnostics")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ferror-limit=5 -fcolor-diagnostics")

add_library(interp SHARED interp.cpp jit.cpp)
set_target_properties(interp PROPERTIES PREFIX "")

target_include_directories(interp PRIVATE ${LLVM_INCLUDE_DIRS})
