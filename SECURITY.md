# Security policy

This project is a research and teaching implementation of the AUTOSAR Adaptive Platform and has not been
security-audited. Do not use it in safety- or security-critical products as is.

To report a vulnerability, open a private security advisory on GitHub (Security tab, "Report a vulnerability")
or contact the repository owner directly. Please do not open a public issue for security problems.

The web console (`ap_dashboard`) binds to loopback by default and runs only a fixed whitelist of demo binaries.
Binding it to a non-loopback address exposes the control endpoints to the network, so do that only on a trusted network.
