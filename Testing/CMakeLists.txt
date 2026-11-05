# Legacy Execution-Management and Communication tests (single executable).
# They read manifests through paths relative to <build>/Testing.
ap_add_test(
  NAME            ${PROJECT_NAME}-ut
  SOURCES         EM/functiongroupstate_test.cpp
                  EM/Paraser_test.cpp
                  EM/functiongroup_test.cpp
                  EM/stateclient.cpp
                  COM/marshal_test.cpp
  DEPENDS         ara_exec_lib
  LABELS          legacy)

# One test executable per functional cluster.
add_subdirectory(core)
add_subdirectory(exec)
add_subdirectory(diag)
if(TARGET ap_dashboard_core)
  add_subdirectory(dashboard)
endif()
if(AP_HAVE_CRYPTO)
  add_subdirectory(crypto)
endif()
add_subdirectory(log)
add_subdirectory(per)
add_subdirectory(phm)
add_subdirectory(tsync)
