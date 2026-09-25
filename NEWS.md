Unreleased
==========

* Bar and OSD text now use scalable monochrome Fontconfig fallback faces when
  the configured primary Xft face lacks a Unicode glyph. Fallback runs are
  centered using their own Xft metrics, while color-font faces are excluded so
  Xft does not accept glyphs it cannot actually rasterize, such as COLR v1.

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
