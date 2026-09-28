rawm 7.0.0 — unreleased
======================

Unified status ownership
------------------------

* Status collection is now an internal rawm subsystem. rawm is the sole
  configuration and lifecycle authority; the standalone `rawmstat` executable,
  configuration file, manuals, and rawmsession sidecar are retired.
* Native status sources run in a fault-isolated child process supervised by
  rawm and communicate through an anonymous `SOCK_SEQPACKET` socket. No status
  X root property, public socket, PID file, or second configuration parser
  remains.
* Status source policy lives under `status.sources` in `rawm.conf`. Native CPU,
  memory, filesystem, network, battery, and clock providers retain in-process
  sampling state; `exec` remains an explicitly bounded extension provider.
* Worker loss marks existing values stale without terminating the window
  manager. rawm reaps and restarts the worker with bounded backoff.
* `rawm --status-debug` runs the same collector path in the foreground for
  configuration and source diagnostics without opening the X display.
* Native ALSA playback-volume status replaces the last command-polled desktop
  measurement. While healthy it is event-driven through libasound mixer poll
  descriptors; the configured period is used only for reconnect attempts.

rawm 6.5.0 — 2026-09-26
=========================

Clipboard history integration
-----------------------------

* `rawclip menu` is now a stock argv-level desktop capability bound to
  `Mod4+v`. rawm owns the global key grab and process launch; rawclip retains
  clipboard capture, exclusion, retention, replay, and menu policy.
* The installed configuration and compiled defaults carry the same
  `clipboard_history` command and binding, with parity tests pinning the
  contract.
* An Xvfb/XTest integration test exercises the real `Mod4+v` key-grab and
  direct-exec path and asserts that rawclip receives exactly the `menu`
  argument.
* The key injection helper now supports ordered key chords while preserving
  existing single-key tests.
* Documentation records rawclip as an independent repository/runtime authority
  and corrects the declared Meson build requirement to 1.6.0.

rawm 6.4.0 — 2026-09-25
=========================

Desktop integration
-------------------

* `rawlock` is now a stock argv-level desktop capability, invoked by the
  built-in `Control+Mod1+Delete` binding (normally Control+Alt+Delete). rawm
  launches the separately maintained locker but does not participate in
  authentication or supervise its lifetime.
* The default-command and binding tests pin the rawlock command and lock chord
  alongside the rawmenu/rawmctl desktop composition.
* The rawm manuals and repository architecture now document the complete
  screen-lock authority boundary and the built-in `Mod4+w` window selector.
* The declared Meson minimum is 1.6.0, matching the maintained test graph's
  use of built executable objects as test arguments.

rawm 6.3.0 — 2026-09-25
=========================

Session lifecycle
-----------------

* `rawmsession` is now the explicit lifecycle authority for a rawm desktop
  session. rawm is the non-respawned primary process; rawmstat and Picom are
  stock supervised sidecars.
* Sidecar exits are restarted after a short delay, but persistent failures are
  disabled after five restart attempts within ten seconds so a broken service
  cannot create an unbounded crash loop.
* SIGTERM/SIGINT perform bounded orderly shutdown. SIGHUP restarts the complete
  managed infrastructure in place and re-reads session configuration while a
  direct SIGHUP to rawm remains the WM-only reload path.
* `/etc/rawm/rawmsession.conf` and XDG user overrides describe direct argv
  vectors only. There is no implicit shell and no generic application
  autostart list.
* Lifecycle tests cover primary status propagation, sidecar cleanup and
  restart, crash-loop suppression, supervisor signals, and in-place session
  restart without requiring an X server.

rawm 6.2.0 — 2026-09-25
=========================

Repository architecture
-----------------------

* The complete rawmstat history is imported without squashing and its current
  sources are built from the rawm repository while rawmstat remains a separate
  process.
* Meson now builds, installs, and tests rawmstat alongside rawm. The
  rawm/rawmstat integration test always uses the in-tree executables, making
  producer/consumer compatibility a repository-local release invariant.
* rawmstat keeps its ISC licensing and provenance explicitly alongside rawm's
  MIT/X sources.
* The rawm-v2 wire codec, state vocabulary, identifier validation, and payload
  limits now live in one private static library shared by rawm and rawmstat;
  neither process reconstructs the other side's protocol rules independently.
* `rawmctl` is now the repository-owned operational CLI. It lists and activates
  managed windows through EWMH, selects windows through `rawmenu` while keeping
  XID identity authoritative, and gets/sets XKB keyboard groups directly. This
  replaces the useful behavior of the historical `rawin`/`rawkb` utilities
  without Perl, wmctrl, xdotool, xdo, shell command construction, or
  title-based reverse lookup.
* The stock command policy now uses `rawmenu-run` for launching, `rawmenu` input
  mode for tag renaming, and `rawmctl window select` on Mod4+w. The inherited
  dmenu defaults and their duplicated presentation flags are no longer part of
  rawm policy.

rawm 6.1.1 — 2026-09-25
=========================

Text rendering
--------------

* Bar and OSD text now use scalable monochrome Fontconfig fallback faces when
  the configured primary Xft face lacks a Unicode glyph. Fallback runs use
  their own Xft metrics and are vertically centered in the rawm text line.
* Color-font fallback is deliberately excluded. Some modern color formats,
  notably COLR v1, can advertise glyph coverage through Fontconfig/Xft while
  producing no visible raster through the Xft rendering path. A monochrome
  fallback such as Noto Emoji keeps the result deterministic.
* Label truncation now preserves UTF-8 codepoint boundaries and reserves the
  ellipsis width before copying the displayed prefix.
* Long-label fitting builds UTF-8 boundaries once and binary-searches the
  largest fitting prefix, avoiding repeated byte-by-byte remeasurement of
  maximum-size status/title strings.

rawm 6.1 — 2026-09-25
=======================

This release turns rawm from a compile-time-customized dwm derivative into a
runtime-configurable X11 window manager with explicit state boundaries and a
behavioral test foundation.

Configuration and build
-----------------------

* Meson is the canonical build system.
* Runtime policy is loaded through libconfig from compiled defaults,
  `/etc/rawm/rawm.conf`, and an XDG user override.
* Configuration validation covers types, ranges, duplicate bindings,
  references, commands, tags, layouts, and stable byte limits.
* `${...}` scalar references let commands and presentation policy reuse final
  merged configuration values without shell expansion.
* The installed system configuration is now tested for deep equivalence with
  compiled defaults.

Window and geometry policy
--------------------------

* Per-tag layout, master count/factor, and bar state are preserved across
  views, including the all-tags sentinel.
* Free-window geometry is distinct from arranged geometry and survives layout
  transitions.
* Floating placement, ICCCM size hints, dialog centering, fullscreen policy,
  and initial attention policy are explicit configuration authorities.
* Direct focus indexing counts only visible clients.
* `window_view` follows the focused client's admitted tag mask without
  confusing the all-tags view with tag one.

Bar, status, and interaction
----------------------------

* The bar uses spaced semantic sections and inset state cells instead of
  occupancy decorations.
* Selected tiled and selected elevated/floating clients have distinct semantic
  authority colors.
* `_RAWM_STATUS_V2` carries structured `id/state/text` status segments and
  supports per-segment mouse actions.
* Tag and layout areas support wrapped wheel navigation.
* Hover feedback is tied to actual bound actions.
* Tag renaming is command-backed and no longer constructs a shell command
  inside the window manager.

Compositor and transient UI
---------------------------

* Managed clients publish `_RAWM_FLOATING`, `_RAWM_SELECTED`, `_RAWM_LAYOUT`,
  and `_RAWM_MONITOR`.
* rawm-owned bar, systray, and OSD windows publish `_RAWM_SURFACE`.
* A transient OSD reports layout/view/routing/master/fullscreen transitions and
  can optionally show admitted floating move/resize geometry.

System tray
-----------

* XEmbed mapped state now controls tray presentation independently of icon
  lifetime.
* A visible fullscreen client suppresses the tray surface and releases its bar
  reservation until fullscreen ends, so desktop chrome cannot overlap the
  monitor-owning client.
* `ui.systray.icon_size` can size and center icon slots independently of a
  taller bar while keeping the automatic bar-height policy available with 0.
* Hidden icons remain docked but consume no rail geometry.
* An empty tray remains unmapped and reserves no bar width.

Testing
-------

* X11 integration tests use isolated Xvfb fixtures with capability-based SKIP
  behavior.
* Behavioral coverage includes status rendering and routing, compositor state,
  placement, gaps, fullscreen/attention rules, geometry, tag presentation,
  systray/XEmbed state, focus indexing, window-view state, command/action
  behavior, signals, and Xinerama monitor routing.
* Xtst-backed input helpers keep the extension library loaded through X display
  teardown, avoiding dangling Xlib extension callbacks.

Compatibility
-------------

The runtime configuration format is intentionally strict. Unknown settings,
invalid bindings, unresolved references, or out-of-range values fail closed
during `rawm -C` or startup rather than being guessed or silently ignored.
