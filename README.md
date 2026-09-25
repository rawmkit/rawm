OVERVIEW
========

`rawm` is a compact X11 desktop control-plane repository centered on a
dynamic tiling window manager derived from suckless `dwm` 6.0. It builds the
`rawm` window manager, the separate `rawmstat` status producer, and the
`rawmctl` operational CLI from one source authority while keeping their
process and runtime-policy boundaries explicit.

The code keeps the small, direct Xlib model of its ancestor while moving
desktop policy out of compile-time headers and into validated runtime
configuration. Major facilities include:

  * layered libconfig policy with `/etc/rawm/rawm.conf` and per-user overrides;
  * scalar `${...}` references resolved after configuration layers are merged;
  * tile, floating, monocle, bottom-stack, horizontal bottom-stack, and grid
    layouts with per-tag state and configurable gaps;
  * explicit free-window geometry, size-hint, centering, fullscreen, attention,
    and cascading-placement policies;
  * semantic tag, layout, title, and status presentation in the built-in bar;
  * the `_RAWM_STATUS_V2` semantic root-property protocol;
  * compositor-facing `_RAWM_FLOATING`, `_RAWM_SELECTED`, `_RAWM_LAYOUT`,
    `_RAWM_MONITOR`, and `_RAWM_SURFACE` state;
  * rawm-owned transient OSD feedback for layout, view, routing, fullscreen,
    master-area, and optional live floating geometry changes;
  * configurable semantic mouse actions, status-segment actions, view/layout
    cycling, and command-backed tag renaming;
  * optional XEmbed systray, Xinerama, per-window keyboard layout, and focused
    window title support;
  * the independently running `rawmstat` producer for the rawm-v2 status
    protocol;
  * `rawmctl` for direct EWMH window inspection/activation, rawmenu-backed
    window selection, and XKB keyboard-group control;
  * one Meson build and behavioral test suite covering both sides of that
    producer/consumer contract with isolated Xvfb fixtures.

`rawm` depends on `libX11`, `libXft`, `fontconfig`, and `libconfig`.
`rawmstat` depends on `libX11` and `libconfig`; `rawmctl` depends on `libX11`.
The `rawmctl window select` command additionally composes at run time with the
separately packaged `rawmenu`. Xinerama support is optional for the window
manager.

The original `dwm` 6.0 sources remain the historical base:

  1. https://git.suckless.org/dwm
  2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz
  3. https://dwm.suckless.org/patches/

See the git history and `NEWS.md` for rawm-specific evolution.
See `docs/architecture.md` for repository, process, library, history, and
security-boundary policy.


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
for example, a terminal command may reuse `${ui.font}` instead of duplicating
the configured face.

The stock desktop command policy composes with the separately packaged
`rawmenu`: `Mod4+r` runs `rawmenu-run`, tag renaming uses `rawmenu` input mode,
and `Mod4+w` runs `rawmctl window select`. These are argv-level defaults, not
hard-coded presentation logic; user configuration may replace any command.

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
display, or `rawm -C -c ./config/rawm.conf` to validate one explicit file over the
built-in defaults. See `rawm.conf(5)` for the complete schema, precedence and
binding semantics.


RELEASE VERIFICATION
====================

Before tagging a release, verify the normal build and behavioral suite from a
clean tree:

    meson setup build
    meson compile -C build
    meson test -C build --print-errorlogs

Then stage the install and inspect the package layout without modifying the
host system:

    rm -rf stage
    DESTDIR="$PWD/stage" meson install -C build
    find stage -type f -print | sort

The staged tree must contain the `rawm`, `rawmstat`, and `rawmctl` binaries,
their manual pages, `rawm.conf(5)`, `rawmstat.conf(5)`, and both configuration
files under `/etc/rawm`. `rawm -C -c config/rawm.conf` is also covered by the
test suite, including parity between the installed configuration and compiled
defaults.

Meson can create a source archive from a clean git checkout with:

    meson dist -C build

Only tag after the build, tests, staged install, and distribution archive are
clean.


LICENSE
=======

The rawm window-manager sources are licensed under the MIT/X Consortium
license in `LICENSE`. The imported rawmstat component remains under the ISC
license in `LICENSES/ISC.txt`; `LICENSES/README.md` maps the current component
paths to their notices.
