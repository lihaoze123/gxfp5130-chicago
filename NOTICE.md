# Source provenance and licenses

This repository consolidates the tested GXFP5130 ChicagoHS implementation. It is
self-contained with respect to the device code; normal build dependencies and
fprintd source are still obtained through Nix. It does not contain proprietary
Windows binaries, private calibration, keys or biometric captures.

Copyright and SPDX notices in individual files remain authoritative:

* Kernel: Void755/gxfp_linux_driver, revision 594c372, with local ChicagoHS work;
  GPL-2.0. See `LICENSES/GPL-2.0.txt`.
* libfprint framework: libfprint contributors and the Void755/libfprint fork
  (base 1f7941e), with character-device discovery and local integration;
  LGPL-2.1-or-later. See `libfprint/COPYING`.
* Userspace transport: Void755/gxfpmoc (base 4b489a7), carried through
  AregShahbazian/gxfp5130-chicagohs (base a49e13802ba40fde02fa6a119eb629fdd86e0c21).
  Original headers and license notices are preserved.
* Chicago matcher: berkekbgz/libfprint-goodix-spi,
  revision 010a665f54089a1632b1ab7be588b316ace934e2, Chicago and CRC sources;
  LGPL-2.1-or-later. Local runtime adapter and test integration are additional
  changes. See `src/algo/match/PORTING.md` within the driver subtree.
* fprintd: upstream 1.94.4, GPL-2.0-or-later. The adaptive persistence patch was
  adapted from the same libfprint-goodix-spi project's fprintd patch.
* Consolidation, Nix packaging and tools retain the licenses of their source
  components; newly written integration files are GPL-2.0-or-later.

The old project names above record provenance, not installation dependencies.
