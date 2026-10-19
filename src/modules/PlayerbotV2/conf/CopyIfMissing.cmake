# Usage: cmake -DSRC=<file> -DDST_DIR=<dir> -P CopyIfMissing.cmake
# Copies SRC into DST_DIR unless a file of that name is already there, so a user's edited copy is never overwritten.
get_filename_component(_name "${SRC}" NAME)
if(NOT EXISTS "${DST_DIR}/${_name}")
  file(MAKE_DIRECTORY "${DST_DIR}")
  file(READ "${SRC}" _content)
  file(WRITE "${DST_DIR}/${_name}" "${_content}")
endif()
