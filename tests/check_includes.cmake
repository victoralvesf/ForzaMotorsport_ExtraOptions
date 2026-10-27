# cmake -DSRC=<src dir> -P check_includes.cmake
# Each library only has its own folder (and those of what it links) on the include path; a relative include with
# "../" would get around that, so it is rejected.
file(GLOB_RECURSE files "${SRC}/*.cpp" "${SRC}/*.hpp" "${SRC}/*.inc")
set(bad "")
foreach(f IN LISTS files)
    file(STRINGS "${f}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<][^\">]*\\.\\.[/\\\\]")
    if(lines)
        file(RELATIVE_PATH rel "${SRC}" "${f}")
        list(APPEND bad "${rel}: ${lines}")
    endif()
endforeach()
if(bad)
    string(REPLACE ";" "\n  " bad "${bad}")
    message(FATAL_ERROR "relative includes across folders:\n  ${bad}")
endif()
message(STATUS "${CMAKE_CURRENT_LIST_FILE}: no relative includes")
