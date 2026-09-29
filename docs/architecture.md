# rawm repository architecture

## Authority

rawm is the X11 window-management authority. Its mutable domain is limited to:

* admitted clients and their authoritative state;
* monitor topology and external-dock work areas;
* tags and views;
* layouts and per-tag layout state;
* focus and stacking;
* tiled/free/fullscreen geometry;
* pointer-driven move, resize, and floating transitions;
* compositor-facing projections of admitted WM state;
* the rawm command/state membrane, including transient semantic feedback publication.

A responsibility with an independent lifecycle, failure domain, public
interface, or policy authority belongs in another repository. Extraction is
complete only when rawm no longer carries a dormant implementation copy.

## External authorities

The rawmkit desktop composes independent repositories:

* `rawctl` consumes the rawm membrane as a command-line client.
* `rawkeys` owns global passive grabs, key routing, argv execution, and
  per-window XKB state.
* `rawpanel` owns persistent dock presentation, panel pointer policy, XEmbed
  system-tray ownership, native status collection, and ALSA state/control.
* `rawsession` owns desktop process topology and supervision.
* transient feedback renderers consume `_RAWM_FEEDBACK`; rawm owns no feedback window or presentation policy.
* `rawwin` is a stateless composition leaf: rawctl client inventory, rawmenu
  selection, rawctl direct focus.
* `rawmenu`, `rawclip`, `rawpass`, and `rawlock` retain their independent
  authority boundaries.

Dependency direction is toward rawm's declared membrane. rawm does not depend
back on these consumers and does not reconstruct their state.

## Control/state membrane

rawm owns the per-screen `_RAWM_CONTROL_SN` X selection. The selection-owner
window is the liveness boundary. It publishes complete authoritative WM state
and admitted-client inventory, advertises supported window-manager commands,
and receives versioned command ClientMessages.

Consumers may observe rawm truth, but may mutate it only through this declared
interface. They must not derive tag/focus/layout authority by crawling the X
client tree or generic EWMH state.

See `control-protocol.md` for the wire contract.

## Dock/work-area boundary

Panel geometry is not rawm authority. External desktop chrome advertises
`_NET_WM_WINDOW_TYPE_DOCK` and `_NET_WM_STRUT_PARTIAL` or `_NET_WM_STRUT`.
rawm observes those standard reservations and derives usable monitor geometry.
Mapping, unmapping, destroying, or changing a dock reservation recomputes work
areas and rearranges tiled clients. Fullscreen remains based on full monitor
geometry.

## Presentation boundary

rawm owns no desktop presentation surface. Persistent panel/status/systray
presentation is external, and transient feedback is published semantically on
`_RAWM_FEEDBACK` for an independent renderer. rawm does not own fonts, text
formatting, OSD geometry, visibility timers, or presentation windows.

The feedback stream is advisory and event-oriented. Authoritative state remains
`_RAWM_STATE` and `_RAWM_CLIENTS`; consumers must never reconstruct WM truth
from feedback records.

## Input boundary

Global keyboard policy is rawkeys authority. rawm retains only client-pointer
mechanics that require admitted geometry and active pointer grabs. In the stock
policy those are move, resize, and explicit floating toggle.

Window-manager operations invoked from rawkeys travel through the rawm membrane;
rawkeys never calls rawm implementation functions or writes rawm-owned X state.

## Configuration boundary

rawm configuration describes only rawm-owned state. The schema contains WM
geometry/policy, tags/layouts/rules, border projection, and admitted-client
pointer mechanics. Extracted authorities are rejected rather than accepted as
inert data.

Global keys and argv execution belong to rawkeys. Persistent panel, status,
systray, and ALSA configuration belong to rawpanel. Cross-repository composition
belongs in integration policy, not in a compatibility parser inside rawm.

## Change discipline

1. Define producer-owned truth and commands before extracting consumers.
2. Create a separate repository when authority/lifecycle/failure ownership is
   independent.
3. Delete extracted implementations from the source repository; do not keep
   fallback copies.
4. Keep producer contract tests with producers and consumer behavior tests with
   consumers.
5. Put cross-repository composition in the integration-policy repository.
6. Introduce shared libraries only after independent consumers prove a stable,
   authority-neutral semantic boundary.
7. Never reconstruct another authority's truth from observations when the owner
   can publish it directly.

## History and licensing

The rawmstat history remains reachable through Git provenance, including the
historical notices that applied to those revisions. Current rawm implementation
files use the repository's MIT/X license. See `LICENSES/README.md` for the
historical boundary.
