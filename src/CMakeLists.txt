# Note that headers are optional, and do not affect add_library, but they will not
# show up in IDEs unless they are listed in add_library.

# https://cliutils.gitlab.io/modern-cmake/chapters/basics/structure.html
# https://gitlab.com/CLIUtils/modern-cmake/tree/master/examples/extended-project
# https://cliutils.gitlab.io/modern-cmake/chapters/basics/example.html

#####################################################################################
#                         SETUP THE ARA LIBS                                        #
#####################################################################################
add_subdirectory(ara)


#####################################################################################
#                         SETUP THE EXTRA LIBS                                      #
#####################################################################################
add_subdirectory(OTA)
# add_subdirectory(SD_c)
# add_subdirectory(SD_s)


# here we are adding the header files to the project tree in visual studio
source_group(
  TREE "${PROJECT_SOURCE_DIR}/src"
  PREFIX "Header Files"
  FILES ${HEADER_LIST})  


#####################################################################################
#                         SETUP THE INSTALLATION TARGETS                            #
#####################################################################################





# # copy headers to build directory with same structure
# foreach(HEADER ${HEADER_LIST})
#   file(RELATIVE_PATH RELATIVE_HEADER ${PROJECT_SOURCE_DIR}/src ${HEADER})
#   get_filename_component(HEADER_PATH ${RELATIVE_HEADER} DIRECTORY)
#   file(COPY ${HEADER} DESTINATION ${CMAKE_BINARY_DIR}/include/${HEADER_PATH})
# endforeach()


# # install the libraries in the CMAKE_INSTALL_LIBDIR
# install(
#   TARGETS
#   ara_core_lib
#   ara_com_lib
#   ara_exec_lib
#   ara_log_lib
#   ara_sm_lib
#   # ara_crypto_lib
#   ara_ucm_lib
#   ota_lib
#   sdc_lib
#   sds_lib
#   EXPORT ara_libs
#   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
# )

# # install the headers
# install(
#   DIRECTORY ${CMAKE_BINARY_DIR}/include/
#   DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/
# )
