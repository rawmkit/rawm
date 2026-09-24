OVERVIEW
========

This directory contains `rawm`, a Ra Window Manager for X.

This `rawm` distribution is a fork of suckless' `dwm` version 6.0 with
the following applied patches:
  * `bstack`
  * `gapless grid`
  * `pertag`
  * `WM_WINDOW_ROLE`
  * runtime-configurable tag visibility (`occupied` or `all`)
  * `nametag`
  * multi-monitor configuration
  * `freetype2` support
  * optional per-window keyboard layout
  * optional window title
  * optional `xinerama` support
  * count monocle/float windows in statusbar
  * explicit monitor/parent centering policy for floating windows and dialogs
  * configure layout `pertag` at startup
  * `restartsig`
  * `_NET_CLIENT_LIST`
  * optional systray support
  * dedicated `_RAWM_STATUS_V2` semantic root-property status protocol
  * explicit free-window placement with deterministic cascading
  * configurable inner/outer layout gaps with optional smart outer gaps

Unlike original `dwm` version 6.0 this distribution depends on
`freetype2`, `fontconfig`, and `libconfig`; `xinerama` is optional.

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
  * Meson >= 1.4.0
  * `libX11`
  * `libXft`
  * `fontconfig`
  * `libconfig`
  * `xinerama` is optional, for Xinerama Extension support
  * `scdoc(1)` to build the manual pages

The test suite additionally uses POSIX `sh(1p)` plus optional test fixtures.
Meson discovers their programs during setup and probes a disposable Xvfb server
before registering the X11 integration tests. Those tests require a usable Xvfb
runtime and `xwininfo`; `xkbcomp` and `xkeyboard-config` metadata are reported
as XKB diagnostics, while the runtime probe is authoritative. The gap tests also
require `xterm`. Individual scripts use standard tools such as `awk`, `sed`,
`grep`, `sort`, and `wc`. Missing programs or an unusable virtual-X fixture are
reported during Meson setup and the affected tests are registered as skips. Once
the fixture starts successfully, failures remain behavioral test failures.


BUILD AND INSTALL
=================

A normal build uses an out-of-tree Meson build directory:

    meson setup build
    meson compile -C build
    meson test -C build
    meson install -C build

`DESTDIR` may be supplied to `meson install` when staging a package. The
default installation prefix is Meson's `/usr/local`; rawm deliberately keeps
its system configuration directory at `/etc`. Both may be changed with the
standard Meson `prefix` and `sysconfdir` options.

Project feature switches live in `meson.options`:

  * `xinerama`: `enabled`, `disabled`, or `auto`; default `enabled`
  * `systray`: built-in XEmbed system tray; default `true`
  * `per_window_keyboard_layout`: per-window XKB layout tracking; default `true`
  * `window_title`: focused-window title rendering in the bar; default `true`

For example, a build without the tray or title support can be configured with:

    meson setup build -Dsystray=false -Dwindow_title=false

Use `meson configure build` to inspect or change an existing build directory.


CONFIGURATION
=============

`rawm` is configured at runtime with libconfig. The installed system file is
`/etc/rawm/rawm.conf`; a user override may be placed at
`$XDG_CONFIG_HOME/rawm/rawm.conf` or, by default,
`$HOME/.config/rawm/rawm.conf`.

Configuration strings can reuse final scalar settings with `${path}` references;
for example, a dmenu command may use `${ui.font}` and
`${ui.colors.normal.background}` instead of duplicating theme values.

Tiled layouts support runtime-configured inner and outer gaps through
`layout.gaps`; smart gaps may suppress the outer margin when only one tiled
client is visible. Newly managed free normal windows use the explicit
`behavior.floating_placement` policy; the default cascade offsets each new
window from the selected visible free client while preserving user-authoritative
ICCCM `USPosition`.

The bar uses semantic tag presentation rather than dwm occupancy markers.
`ui.bar.tags.visibility = "occupied"` shows only occupied and currently viewed
tags, while `"all"` keeps the complete configured tag row. Selected, urgent,
and ordinary visible tags are distinguished by the existing color schemes, and
rendering and mouse hit testing share the same visibility state.

Status input is explicit policy too. `status.protocol = "rawm-v2"` consumes
semantic `id/state/text` records from the `_RAWM_STATUS_V2` root property.
`normal`, `warning`, and `critical` map to rawm's normal, urgent, and error
palettes; separators and all presentation remain rawm policy. The legacy root
`WM_NAME` channel and in-band color controls are not used.

Use `rawm -C` to validate the effective configuration without opening the X
display, or `rawm -C -c ./rawm.conf` to validate one explicit file over the
built-in defaults. See `rawm.conf(5)` for the complete schema, precedence and
binding semantics.


LICENSE
=======

`rawm` is licensed through MIT/X Consortium License.
See LICENSE file for copyright and license details.
