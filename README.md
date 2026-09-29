OVERVIEW
========

`rawm` is a compact X11 window manager derived from suckless `dwm` 6.0.
This repository owns window-management state only: client admission, monitors,
tags, layouts, focus, geometry, external-dock work areas, pointer-driven client
manipulation, compositor-facing state, and the rawm control/state membrane.

The rawmkit desktop deliberately keeps independent authorities in independent
repositories:

  * `rawctl` is the command-line client for the rawm membrane;
  * `rawkeys` owns global keyboard policy and per-window XKB state;
  * `rawpanel` owns persistent desktop chrome, systray, status, and ALSA state;
  * `rawsession` owns session process topology;
  * `rawwin` composes interactive window selection through rawctl and rawmenu.

rawm does not start, supervise, configure, or reconstruct those authorities.

Current rawm facilities include:

  * layered libconfig window-management policy;
  * tile, floating, monocle, bottom-stack, horizontal bottom-stack, and grid
    layouts with per-tag state and configurable gaps;
  * explicit free-window geometry, size-hint, centering, fullscreen, attention,
    and cascading-placement policies;
  * authoritative monitor/tag/layout/focus and admitted-client snapshots through
    a versioned X selection membrane;
  * standard EWMH dock/strut observation for external work-area reservations;
  * compositor-facing `_RAWM_FLOATING`, `_RAWM_SELECTED`, `_RAWM_LAYOUT`, and
    `_RAWM_MONITOR` client state;
  * pointer-driven client move, resize, and explicit floating control;
  * semantic transient-feedback events for independent presentation consumers.

The configuration schema is authority-scoped and fail-closed. Historical
`status`, `commands`, the historical `input` root, `ui.bar`, and `ui.systray`
settings are rejected rather than retained as inert compatibility state.

The original `dwm` 6.0 sources remain the historical base:

  1. https://git.suckless.org/dwm
  2. https://dl.suckless.org/dwm/dwm-6.0.tar.gz
  3. https://dwm.suckless.org/patches/

See `docs/architecture.md` for repository authority and
`docs/control-protocol.md` for the rawm command/state contract.


REQUIREMENTS
============

Build time
----------
  * C99 compiler
  * Meson >= 1.6.0
  * `libX11`
  * `libconfig`
  * optional `xinerama`
  * `scdoc(1)` for manual pages

There is no Xft/Fontconfig, ALSA, XEmbed tray, status-worker, or global-key
runtime dependency in rawm. Presentation is outside the window-manager process.

The test suite additionally uses POSIX `sh(1p)` and optional isolated X11
fixtures. Meson probes Xvfb at setup time and registers integration tests as
skips only when the fixture itself is unavailable. Once a fixture starts,
behavioral failures are failures rather than silent skips.


BUILD AND INSTALL
=================

A normal out-of-tree build is:

    meson setup build
    meson compile -C build
    meson test -C build --print-errorlogs
    meson install -C build

`DESTDIR` may be used for package staging. The default install prefix is
Meson's `/usr/local`; the rawm system configuration directory remains `/etc`.

The only project feature switch is currently `xinerama`, which accepts Meson's
`enabled`, `disabled`, or `auto` feature values.


CONFIGURATION
=============

rawm reads `/etc/rawm/rawm.conf` and then the XDG user override, normally
`$HOME/.config/rawm/rawm.conf`. `rawm -c PATH` loads built-in defaults followed
by exactly `PATH`. `rawm -C` validates the resulting configuration without
opening X.

Window-management policy includes rules, tags, layouts, gaps, client geometry,
attention/fullscreen behavior, client border colors, and client-pointer bindings. Global key bindings and program launch commands are external rawkeys
policy. Persistent panel/status/systray policy is external rawpanel policy.

External panels reserve space through standard `_NET_WM_WINDOW_TYPE_DOCK` plus
`_NET_WM_STRUT_PARTIAL` or `_NET_WM_STRUT`; rawm has no panel-specific IPC or
lifecycle dependency.


CONTROL MEMBRANE
================

rawm owns `_RAWM_CONTROL_SN` per X screen. Its owner window publishes the
version, supported commands, authoritative window-manager snapshot, admitted
client inventory, and advisory `_RAWM_FEEDBACK` events. Commands arrive as
`_RAWM_COMMAND` ClientMessage events.

Consumers must use that membrane rather than crawling windows or reconstructing
rawm tag/focus semantics from EWMH. The standalone `rawctl` repository is the
reference command-line consumer.

The protocol intentionally contains no commands for spawning processes, volume,
panel visibility, status collection, keyboard layout, or session supervision.


SESSION LIFECYCLE
=================

Session lifecycle is outside this repository. A typical rawmkit X session is:

    export XDG_CURRENT_DESKTOP=rawm
    exec rawsession

`rawsession` normally supervises rawm together with independent sidecars such as
rawkeys, rawpanel, rawclipd, and Picom.


RELEASE VERIFICATION
====================

Before tagging:

    rm -rf build stage
    meson setup build
    meson compile -C build
    meson test -C build --print-errorlogs
    DESTDIR="$PWD/stage" meson install -C build
    find stage -type f -print | sort

The staged rawm package should contain the `rawm` binary, rawm manual pages, and
rawm configuration only. It must not install rawctl, rawkeys, rawpanel,
rawsession, rawwin, status workers, systray utilities, or session entries.


LICENSE
=======

The current rawm source tree is licensed under the MIT/X Consortium license in
`LICENSE`. Historical imported rawmstat revisions retain their historical
notices in Git history; see `LICENSES/README.md` for provenance.
