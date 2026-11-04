# Legacy file logger ("Log" class) used by the platform processes.
# New code should use the spec-compliant ara::log (src/ara/log).
ap_add_cluster(TARGET ara_log_legacy_lib LEGACY DEPENDS spdlog::spdlog)
