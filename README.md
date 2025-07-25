OVERVIEW
========

This directory contains `rawm`, a raw dynamic window manager for X.

This `rawm` distribution is a fork of suckless' `dwm` version 6.0 with
the following applied patches:
  * `bstack`
  * `gapless grid`
  * `pertag`
  * `WM_WINDOW_ROLE`
  * `hide vacant tags`
  * `nametag`
  * multi-monitor configuration
  * `freetype2` support
  * optional per-window keyboard layout (`-DPWKL`)
  * optional window title (`-DWINTITLE`)
  * optional `xinerama` support (`-DXINERAMA`)
  * count monocle/float windows in statusbar
  * `iscentered` rule for float windows
  * configure layout `pertag` at startup
  * `restartsig`
  * `_NET_CLIENT_LIST`
  * optional systray support (`-DSYSTRAY`)
  * `statuscolor` patch
  * optional auto centering of floating popup windows

Unless original `dwm` version 6.0 this distribution depends on
`freetype2` and `xinerama` (optional).

See git log for further differences.

The original sources can be downloaded from:
  1. https://git.suckless.org/dwm                (git)
  2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz  (tarball)
  3. https://dwm.suckless.org/patches/           (patches)


REQUIREMENTS
============

Build time
----------
  * C99 compiler
  * POSIX `sh(1p)`, `make(1p)` and "mandatory utilities"
  * `libX11`
  * `freetype2`
  * `fontconfig`
  * `xinerama` is optional, for Xinerama Extension support
  * `scdoc(1)` to build manual page


INSTALL
=======

The shell commands `make && make install` should build and install
this package.

See `config.mk` file for configuration parameters.


CUSTOMIZATION
=============

`rawm` can be customized by creating a custom `config.h` file and
(re)compiling the source code.


LICENSE
=======

`rawm` is licensed through MIT/X Consortium License.
See LICENSE file for copyright and license details.
