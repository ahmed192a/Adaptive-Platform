# ---------------------------------------------------------------------------
# ap_add_cluster()  - one call per AUTOSAR functional cluster / library.
#
#   ap_add_cluster(
#       NAME core                       # -> target ara_core_lib, alias ara::core
#       [PREFIX ara]                    # target prefix and include sub-folder
#       [SOURCES_DIR <dir>]             # default: CMAKE_CURRENT_SOURCE_DIR
#       [DEPENDS a b ...]               # PUBLIC link dependencies
#       [PRIVATE_DEPENDS x y ...]       # PRIVATE link dependencies
#       [LEGACY]                        # relaxed warnings (code not yet cleaned)
#       [STATIC|SHARED])                # default follows AP_BUILD_SHARED
#
# Sources are collected with CONFIGURE_DEPENDS so adding a file re-runs CMake
# automatically.  A cluster without any .cpp becomes an INTERFACE library.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

function(ap_add_cluster)
  cmake_parse_arguments(AP "STATIC;SHARED;LEGACY"
                           "NAME;TARGET;PREFIX;SOURCES_DIR;INCLUDE_ROOT"
                           "DEPENDS;PRIVATE_DEPENDS" ${ARGN})
  if(NOT AP_NAME AND NOT AP_TARGET)
    message(FATAL_ERROR "ap_add_cluster: NAME (or TARGET) is required")
  endif()
  if(NOT AP_PREFIX)
    set(AP_PREFIX ara)
  endif()
  if(NOT AP_SOURCES_DIR)
    set(AP_SOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  if(NOT AP_INCLUDE_ROOT)
    set(AP_INCLUDE_ROOT ${PROJECT_SOURCE_DIR}/src)
  endif()

  if(AP_TARGET)
    set(tgt ${AP_TARGET})
  else()
    set(tgt ${AP_PREFIX}_${AP_NAME}_lib)
  endif()
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${AP_SOURCES_DIR}/*.cpp)
  file(GLOB_RECURSE hdrs CONFIGURE_DEPENDS ${AP_SOURCES_DIR}/*.h)

  if(NOT srcs)
    add_library(${tgt} INTERFACE)
    set(scope INTERFACE)
  else()
    if(AP_STATIC)
      add_library(${tgt} STATIC ${srcs} ${hdrs})
    elseif(AP_SHARED)
      add_library(${tgt} SHARED ${srcs} ${hdrs})
    else()
      add_library(${tgt} ${LIB_BUILD_TYPE} ${srcs} ${hdrs})
    endif()
    set(scope PUBLIC)
    if(AP_LEGACY)
      target_link_libraries(${tgt} PRIVATE ap::legacy)
    else()
      target_link_libraries(${tgt} PRIVATE ap::warnings)
    endif()
    target_link_libraries(${tgt} PRIVATE ap::coverage ap::sanitizers)
    target_compile_features(${tgt} PUBLIC cxx_std_17)
    set_target_properties(${tgt} PROPERTIES
      VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
      FOLDER "ara")
    if(AP_NAME)
      set_target_properties(${tgt} PROPERTIES EXPORT_NAME ${AP_NAME})
    endif()
  endif()
  if(AP_NAME)
    add_library(${AP_PREFIX}::${AP_NAME} ALIAS ${tgt})
  endif()

  target_include_directories(${tgt} ${scope}
    $<BUILD_INTERFACE:${AP_INCLUDE_ROOT}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  foreach(dep IN LISTS AP_DEPENDS)
    if(dep MATCHES "^(ara|ota|sdc|sds)[_:]")
      target_link_libraries(${tgt} ${scope} ${dep})
    elseif(dep STREQUAL "pthread")
      target_link_libraries(${tgt} ${scope} Threads::Threads)
    else()
      # external targets are not part of our export set: reference them by
      # name for installed consumers (resolved by find_dependency()).
      target_link_libraries(${tgt} ${scope} $<BUILD_INTERFACE:${dep}> $<INSTALL_INTERFACE:${dep}>)
    endif()
  endforeach()
  if(srcs)
    foreach(dep IN LISTS AP_PRIVATE_DEPENDS)
      target_link_libraries(${tgt} PRIVATE ${dep})
    endforeach()
  endif()

  if(AP_INSTALL)
    install(TARGETS ${tgt} EXPORT AdaptivePlatformTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    file(RELATIVE_PATH rel ${AP_INCLUDE_ROOT} ${AP_SOURCES_DIR})
    install(DIRECTORY ${AP_SOURCES_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${rel}
            FILES_MATCHING PATTERN "*.h")
  endif()
endfunction()

# Backward compatible shim for the old macro.  Prefer ap_add_cluster().
function(create_library)
  cmake_parse_arguments(CL "STATIC;SHARED" "LIBNAME" "SOURCES_DIR;INCLUDES;DEPENDS" ${ARGN})
  set(type_arg "")
  if(CL_STATIC)
    set(type_arg STATIC)
  endif()
  ap_add_cluster(TARGET ${CL_LIBNAME} SOURCES_DIR ${CL_SOURCES_DIR}
                 DEPENDS ${CL_DEPENDS} ${type_arg} LEGACY)
endfunction()
