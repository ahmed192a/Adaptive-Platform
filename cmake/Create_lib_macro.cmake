# create the macro create_library
macro(create_library)

  # set the default values
  set(options STATIC SHARED)
  set(oneValueArgs LIBNAME)
  set(multiValueArgs SOURCES_DIR INCLUDES DEPENDS)


  # parse the arguments
  cmake_parse_arguments(create_library "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    #  check if options are set
    if (create_library_STATIC)
      set(create_library_TYPE "STATIC")
    elseif(create_library_SHARED)
        set(create_library_TYPE "SHARED")
    endif()


  # Glop the source files
  file(GLOB_RECURSE create_library_SOURCES ${create_library_SOURCES_DIR}/*.cpp ${create_library_SOURCES_DIR}/*.h)

  
  # check if the library type is STATIC or DYNAMIC
  if (${create_library_TYPE} STREQUAL "STATIC")
    # create the library
    add_library(
      ${create_library_LIBNAME}
      STATIC
      ${create_library_SOURCES}
    )
  else()
    # create the library
    add_library(
      ${create_library_LIBNAME}
      SHARED
      ${create_library_SOURCES}
    )
  endif()

  # set the include directory to detect header files
  target_include_directories(
    ${create_library_LIBNAME} 
    PUBLIC 
    ${create_library_INCLUDES}
  )
  # check if list create_library_DEPENDS is not empty
  if (NOT create_library_DEPENDS STREQUAL "")
    if (create_library_STATIC)
      set(create_library_DEPENDS ${create_library_DEPENDS} -static)
    endif()
    
    # link the library with its external dependencies
    target_link_libraries(
      ${create_library_LIBNAME}
      PUBLIC
      ${create_library_DEPENDS}
    )
  endif()

  # add -std=gnu++17 to the compile flags
  target_compile_options(
    ${create_library_LIBNAME}
    PUBLIC
    -std=gnu++17
  )

  # set c++17 standard
  target_compile_features(
    ${create_library_LIBNAME}
    PUBLIC
    cxx_std_17
  )
  
endmacro()