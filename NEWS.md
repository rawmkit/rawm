rawm 7.0.0 — unreleased
======================

* Managed-client raises now preserve mapped non-dock override-redirect surfaces
  already above the client, so floating/fullscreen/layout restacks do not jump
  over OSD, popup-menu, tray-menu, lock, or similar transient presentation.
  Docks remain outside that ceiling so fullscreen can still cover desktop chrome.

* `_RAWM_STATE` advances to version 2 and publishes authoritative monitor work-area geometry (`wx/wy/ww/wh`) beside full monitor rectangles so external presentation can respect dock reservations without reconstructing struts.

Authority cleanup
-----------------

* The in-process transient OSD has been extracted from rawm authority. rawm now
  publishes semantic `_RAWM_FEEDBACK` records and owns no feedback window, font,
  text rendering, visibility timer, or OSD geometry policy.
* Xft and Fontconfig are no longer rawm dependencies. `ui.font`, `ui.osd`,
  `layout.symbols`, and foreground/background presentation colors have been
  removed from the fail-closed rawm schema.
* The proven rawkeys/rawpanel cutover is now reflected in rawm itself: global
  key grabs, command spawning, per-window XKB tracking, built-in bar/status,
  native ALSA control, XEmbed systray ownership, and the legacy in-tree
  `rawmctl` implementation have been deleted from the runtime/build graph.
* Pointer-driven client move, resize, and floating transitions remain rawm
  authority. Persistent and transient presentation are external.
* WM action tests now drive the rawm control membrane directly instead of
  depending on the deleted global-key dispatcher. Tests owned solely by the
  extracted panel/status/systray/input authorities have left this repository.
* rawm no longer links ALSA, Xft, or Fontconfig.
* The configuration schema now matches runtime authority. Historical `status`,
  `commands`, global-key/general-input, bar, systray, status-click, and native
  volume settings are rejected instead of surviving as inert parser state.
* Client pointer policy is expressed by the narrow `pointer.bindings` schema;
  only move, resize, and explicit floating toggle remain configurable there.
* The obsolete private status codec and status configuration parser have been
  deleted from the build graph.


Input authority cutover
-----------------------

* The legacy compiled per-window XKB path is now opt-in. Standalone rawkeys is
  the default owner for global keyboard grabs and per-window keyboard-group
  policy; rawm keeps only pointer-driven client manipulation.

Window-manager authority membrane
---------------------------------

* The rawm control membrane publishes `_RAWM_CLIENTS` as an authoritative
  admitted-client inventory and accepts `_RAWM_CMD_FOCUS_CLIENT`. External
  selectors no longer need to reconstruct rawm client authority from EWMH.
* Changes to admitted client title/class metadata republish `_RAWM_CLIENTS`, so
  membrane consumers do not need to read client properties behind rawm's back.
* rawm now owns an explicit versioned X11 control/state membrane for its
  window-management authority. Consumers discover a per-screen selection owner
  rather than reconstructing rawm semantics from EWMH or the client tree.
* `_RAWM_STATE` on the control owner is a complete semantic snapshot of monitor,
  tag, layout, focus, master-area, occupancy, and urgency state. Property changes
  are snapshot replacement notifications, not an incremental event log.
* `_RAWM_COMMAND` accepts only window-manager operations and advertises the
  supported command atoms on the control owner. Spawn, panel, status, volume,
  keyboard/XKB, and session operations are deliberately absent.
* The control authority is tied to an X selection owner window. X destroys that
  owner and clears the selection when rawm exits, so stale state cannot survive
  a crashed or disconnected window manager.
* Integration coverage drives view, layout, master-factor, and tag-name changes
  exclusively through the external membrane and verifies that non-WM commands
  are rejected without changing admitted state.

Session authority extraction
----------------------------

* Desktop process-lifecycle authority is now the standalone `rawsession`
  repository. rawm no longer builds or installs the former `rawmsession`
  executable, configuration, manuals, lifecycle tests, or X11 session entry.
* rawm packages contain only window-manager-owned runtime surfaces; the
  standalone rawsession package owns `/etc/rawsession/rawsession.conf` and the
  `rawm.desktop` session launcher.
* The extraction preserves lifecycle semantics without a compatibility copy in
  rawm: one primary process defines session lifetime, sidecars have bounded
  restart policy, and session reload/shutdown remain rawsession concerns.

Panel and status authority extraction
-------------------------------------

* Persistent dock presentation, XEmbed systray ownership, native status
  collection, and ALSA state/control are standalone rawpanel authority. rawm
  communicates no private panel lifecycle and derives work areas only from
  standard EWMH dock struts.
* The native status work developed during the transition moved with rawpanel;
  rawm retains no collector process, provider implementation, or ALSA control
  path.

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
