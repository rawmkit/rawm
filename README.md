OVERVIEW
========

`rawm` is a compact X11 window manager derived from suckless `dwm` 6.0.
The repository currently still contains the transitional in-tree `rawmctl`
while that mixed legacy interface is being retired. Session lifecycle has been
extracted to the standalone `rawsession` repository. New cross-component
behavior must use explicit contracts rather than adding another responsibility
to the window-manager process or repository.
Status collection is still an internal rawm worker until panel/status authority
is extracted.

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
  * a fault-isolated status worker supervised privately by rawm over an
    anonymous message socket;
  * compositor-facing `_RAWM_FLOATING`, `_RAWM_SELECTED`, `_RAWM_LAYOUT`,
    `_RAWM_MONITOR`, and `_RAWM_SURFACE` state;
  * a versioned rawm control/state membrane owned by an X selection window,
    including authoritative monitor/tag state and admitted-client inventory;
    external consumers receive authoritative monitor/tag/layout/focus snapshots
    and send only window-manager commands instead of reconstructing rawm truth;
  * rawm-owned transient OSD feedback for layout, view, routing, fullscreen,
    master-area, and optional live floating geometry changes;
  * configurable semantic mouse actions, status-segment actions, view/layout
    cycling, and command-backed tag renaming;
  * optional XEmbed systray, Xinerama, per-window keyboard layout, and focused
    window title support;
  * transitional `rawmctl` behavior for direct EWMH window
    inspection/activation, rawmenu-backed window selection, and XKB
    keyboard-group control; this mixed interface is not the new rawm control
    contract and will not be extracted verbatim;
  * stock `Mod4+v` clipboard-history invocation through separately packaged
    `rawclip`, while rawm retains global-key authority;
  * one Meson build and behavioral test suite covering rawm, its private status
    worker, and session integration with isolated Xvfb fixtures.

`rawm` depends on `libX11`, `libXft`, `fontconfig`, `libconfig`, and `alsa-lib`.
The transitional in-tree `rawmctl` depends on `libX11`. The
`rawmctl window select` command additionally composes at run time with the
separately packaged `rawmenu`; the
stock clipboard-history command similarly composes with separately packaged
`rawclip`. Neither is a link-time dependency of rawm. Xinerama support is
optional for the window manager.

The original `dwm` 6.0 sources remain the historical base:

  1. https://git.suckless.org/dwm
  2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz
  3. https://dwm.suckless.org/patches/

See the git history and `NEWS.md` for rawm-specific evolution.
See `docs/architecture.md` for repository, process, library, history, and
security-boundary policy. `docs/control-protocol.md` defines the rawm
window-management command/state contract.


REQUIREMENTS
============

Build time
----------
  * C99 compiler
  * Meson >= 1.6.0
  * `libX11`
  * `libXft`
  * `fontconfig`
  * `libconfig`
  * `alsa-lib`
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

The stock desktop command policy composes with separately packaged tools:
`Mod4+r` runs `rawmenu-run`, tag renaming uses `rawmenu` input mode, `Mod4+w`
runs `rawmctl window select`, `Mod4+v` runs `rawclip menu`, and
`Control+Mod1+Delete` invokes `rawlock`. These are argv-level defaults, not
hard-coded presentation, clipboard-history, or authentication logic; user
configuration may replace any command. rawm owns the global key grabs and
launches the configured programs, but does not absorb rawclip history policy or
rawlock authentication/lifecycle authority.

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

When `status.enabled` is true, rawm starts a private status worker from the
validated `status.sources` configuration. The worker collects semantic
`id/state/text` snapshots and sends them to its parent through an anonymous
`SOCK_SEQPACKET` socket. Native ALSA status joins the same event loop through
libasound mixer poll descriptors, so volume changes are reported by mixer events
rather than command polling. Native `volume_up`, `volume_down`, and `volume_mute`
actions send control requests to that same worker and mixer authority. `normal`,
`warning`, `critical`, and `stale` are
source semantics; separators, colors, hit testing, and all other presentation
remain rawm policy. The worker has no independent configuration file, public
socket, PID file, X root property, or service lifecycle.

Use `rawm -C` to validate the effective configuration without opening the X
display, or `rawm -C -c ./config/rawm.conf` to validate one explicit file over the
built-in defaults. See `rawm.conf(5)` for the complete schema, precedence and
binding semantics.



SESSION LIFECYCLE
=================

Session lifecycle is not owned or installed by rawm. The standalone
`rawsession` package provides the rawmkit process supervisor and X11 session
entry. Its stock policy starts rawm as the primary process and supervises
long-lived sidecars such as Picom.

For `startx`, a typical startup is:

    export XDG_CURRENT_DESKTOP=rawm
    exec rawsession

A rawm package therefore installs no session supervisor, no session
configuration, and no `xsessions` entry. This keeps process-lifecycle authority
outside the window-manager repository. See `rawsession(1)` and
`rawsession.conf(5)` from the standalone rawsession package.


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

The staged tree must contain the `rawm` and transitional in-tree `rawmctl`
binaries, their manual pages, `rawm.conf(5)`, and `/etc/rawm/rawm.conf`. It must
not contain a `rawmstat` or `rawmsession` executable, session manual, session
configuration, or X11 session entry. Session lifecycle belongs to the standalone
`rawsession` package.

Meson can create a source archive from a clean git checkout with:

    meson dist -C build

Only tag after the build, tests, staged install, and distribution archive are
clean.


LICENSE
=======

The current rawm source tree is licensed under the MIT/X Consortium license in
`LICENSE`. The preserved pre-integration rawmstat history retains the notices
that applied to its historical revisions; see `LICENSES/README.md` for the
provenance boundary.
