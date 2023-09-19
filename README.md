OVERVIEW
--------
This directory contains dwm, a dynamic window manager for X.

This dwm distribution is a fork of suckless' dwm version 6.0 with the
following applied patches:
- bstack
- gapless grid
- pertag
- WM_WINDOW_ROLE
- hide vacant tags
- nametag
- multi-monitor configuration
- freetype2 support
- optional per-window keyboard layout (-DPWKL)
- optional window title (-DWINTITLE)
- optional xinerama support (-DXINERAMA)
- counted monocle windows in bar
- "iscentered" rule for float windows
- configure layout pertag at startup
- restartsig
- restoreafterrestart

Unless original dwm 6.0 this dwm distribution depends on freetype2 and
xinerama.

See git log for further differences.

The original sources can be downloaded from:
1. https://git.suckless.org/dwm                (git)
2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz  (tarball)
3. https://dwm.suckless.org/patches/           (patches)


REQUIREMENTS
------------
**Build time**:
- C99 compiler
- POSIX sh(1p), make(1p) and "mandatory utilities"
- libX11
- freetype2
- fontconfig
- xinerama is optional, for Xinerama Extension support


INSTALL
-------
The shell commands `make && make install` should build and install
this package.  See `config.mk` file for configuration parameters.


CUSTOMIZATION
-------------
dwm can be customized by creating a custom `config.h` file and
(re)compiling the source code.


LICENSE
-------
dwm is licensed through MIT/X Consortium License.
See LICENSE file for copyright and license details.
