# SOME/IP and SOME/IP-SD wire-format conformance tests (AUTOSAR R25-11 PRS).
ap_add_test(
  NAME      ara_com_ut
  SOURCES   someip_message_test.cpp
            someip_sd_test.cpp
            com_api_test.cpp
  DEPENDS   ara_com_lib
  LABELS    com)
