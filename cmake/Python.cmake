include_guard(GLOBAL)

# Python runs on the build host, including when cross-compiling Android or Web.
get_filename_component(_ikigai_python_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(CMAKE_HOST_WIN32)
    set(_ikigai_project_python "${_ikigai_python_root}/.venv/Scripts/python.exe")
else()
    set(_ikigai_project_python "${_ikigai_python_root}/.venv/bin/python")
endif()
if(NOT DEFINED Python3_EXECUTABLE AND EXISTS "${_ikigai_project_python}")
    set(Python3_EXECUTABLE "${_ikigai_project_python}" CACHE FILEPATH "Python interpreter for engine tools")
endif()
set(Python3_FIND_VIRTUALENV FIRST)
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
