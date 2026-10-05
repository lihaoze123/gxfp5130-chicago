# Chicago matcher provenance

The Chicago and CRC matcher sources were imported from
berkekbgz/libfprint-goodix-spi at
010a665f54089a1632b1ab7be588b316ace934e2, under LGPL-2.1-or-later.
Original copyright and SPDX headers are preserved.

The initial port changed the CRC include path. Local integration additionally
adds `gxfp_chicago_runtime_prepare_enhanced_probe`: feature extraction uses the
native enhanced image while retaining raw same-frame auxiliary fields. It then
uses the ordinary Chicago probe representation for matching and study.

The native image path lives separately in `src/algo/image/chicago_native.c`.
The carried matcher tests include deterministic cases and optional factory-vector
cases, which skip when no private vectors are supplied. `chicago_chain_test.c`
checks the integrated synthetic chain without a sensor, private calibration or
DLL. These tests do not establish real biometric acceptance rates or complete
factory parity.
