ABOUT
-----
This directory contains _dwm_, a dynamic window manager for X.

This _dwm_ distribution is a fork of suckless' _dwm_ version 6.0 with
the following applied patches:
  * bstack
  * gapless grid
  * pertag
  * `WM_WINDOW_ROLE`
  * hide vacant tags
  * nametag
  * multi-monitor configuration
  * freetype2 support
  * optional per-window keyboard layout (`-DPWKL`)
  * optional window title (`-DWINTITLE`)
  * optional xinerama support (`-DXINERAMA`)
  * counted monocle windows in bar
  * "iscentered" rule for float windows
  * configure layout pertag at startup
  * restartsig

Unless original _dwm_ 6.0 this _dwm_ distribution depends on
_freetype2_ and _xinerama_.

The original sources can be downloaded from:
  1. https://git.suckless.org/dwm                (git)
  2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz  (tarball)
  3. https://dwm.suckless.org/patches/           (applied patches)

REQUIREMENTS
------------
Build time:
  * c99 compiler
  * POSIX sh(1p), make(1p) and "mandatory utilities"
  * libX11
  * freetype2
  * fontconfig
  * xinerama (optional)

INSTALL
-------
The shell commands `make && make install` should build and install
this package.  See _config.mk_ file for configuration parameters.

_dwm_ can be customized by creating a custom _config.h_ file and
(re)compiling the source code.

LICENSE
-------
_dwm_ is licensed through MIT/X Consortium License.
See _LICENSE_ file for copyright and license details.

<!-- vim:sw=2:ts=2:sts=2:et:cc=72:tw=70
End of file. -->
