# QtADS runs `git describe --tags` during configure. Our FetchContent pin is a
# commit SHA and can be shallow/no-tag, which makes git print
# "fatal: No names found" even though configure succeeds. Pin the known version
# from CMakeLists.txt instead so dependency configure is quiet and deterministic.
if(NOT DEFINED FILE OR NOT EXISTS "${FILE}")
    message(FATAL_ERROR "qtads_suppress_git_describe: FILE not found: ${FILE}")
endif()

file(READ "${FILE}" _qtads_cmake)
string(REPLACE
    "include(GetGitRevisionDescription)\n    git_describe(GitTagVersion --tags)"
    "set(GitTagVersion \"4.5.0\")"
    _qtads_cmake
    "${_qtads_cmake}")
file(WRITE "${FILE}" "${_qtads_cmake}")

get_filename_component(_qtads_root "${FILE}" DIRECTORY)
set(_versioning "${_qtads_root}/cmake/modules/Versioning.cmake")
if(EXISTS "${_versioning}")
    file(READ "${_versioning}" _versioning_cmake)
    string(REGEX REPLACE
        "execute_process\\([^)]+COMMAND git describe --tags --dirty[^)]+\\)"
        "set(GIT_DESC_RAW \"4.5.0\")"
        _versioning_cmake
        "${_versioning_cmake}")
    file(WRITE "${_versioning}" "${_versioning_cmake}")
endif()
