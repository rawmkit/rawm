dwm - dynamic window manager for X
==================================
dwm is a dynamic window manager for X.

This dwm distribution is a fork of suckless' dwm version 6.0 with the
following patches:
 * bstack
 * gapless grid
 * pertag
 * systray (removed)
 * `WM_WINDOW_ROLE`
 * hide vacant tags
 * nametag
 * multi-monitor configuration
 * freetype2 support
 * optional per-window keyboard layout (-DPWKL)
 * optional window title (-DWINTITLE)
 * optional xinerama support
 * counted monocle windows in bar
 * "iscentered" rule for float windows
 * configure layout pertag at startup

Unless original dwm 6.0 this dwm distribution depends on: freetype2
and xinerama.

The original sources can be downloaded from:

  1. https://git.suckless.org/dwm
  2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz
  3. https://dwm.suckless.org/patches/ (applied patches)


Dependencies
------------
Build time:
- c99 compiler
- make(1p), sh(1p) and other POSIX utilities like sed(1p), mkdir(1p),
  cp(1p), rm(1p)
- libX11
- freetype2
- xinerama (optional)


Install
-------
The shell commands `make; make install` should build and install this
package.  See `config.mk` file for configuration parameters.

dwm can be customized by editing `config.h` and (re)compiling the
source code.


License and Copyright
---------------------
dwm is licensed through MIT/X Consortium License.
See LICENSE file for copyright and license details.


<!-- vim:ft=markdown:sw=2:ts=2:sts=2:et:cc=72:tw=70
End of file. -->
