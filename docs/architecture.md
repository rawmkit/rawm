# rawm repository architecture

## Repository authority

The repository is currently in a decomposition transition. The target rule is
one authority and one independently releasable failure domain per repository.
No new subsystem should be added to this repository merely because rawm can
currently call it directly.

`rawm` itself is the window-management authority: client admission, monitor and
tag state, layouts, focus, geometry, and pointer-driven window manipulation.
The first extraction prerequisite is now an explicit control/state membrane; see
`control-protocol.md`. External components command rawm through that membrane
and consume rawm-published state instead of reconstructing rawm truth from EWMH
or the X window tree.

The current tree still contains transitional responsibilities that are planned
for extraction rather than further integration:

* the legacy in-tree `rawmctl` still mixes EWMH window operations and XKB
  keyboard control. It is not the authority or specification for the rawm
  membrane. The standalone `rawctl` repository already consumes that membrane
  without reconstruction, and `rawwin` composes client selection through
  `rawctl` plus `rawmenu`. The in-tree legacy utility remains only until its
  obsolete implementation is deleted.
* session lifecycle has been extracted to the standalone `rawsession`
  repository; rawm no longer builds, installs, tests, or configures it.
* the standalone `rawpanel` repository now owns the persistent dock, systray,
  status providers, ALSA state/control, and panel pointer policy. The equivalent
  built-in rawm implementations remain temporarily in this tree only for the
  controlled live cutover and are disabled by integration policy before
  deletion. OSD remains a separate unresolved presentation boundary and is not
  assigned to rawpanel merely because both draw X11 surfaces.
* global keyboard grabs, command dispatch, and per-window XKB policy belong to
  the standalone `rawkeys` authority. The legacy compiled per-window XKB path is
  now opt-in rather than a rawm default. Pointer-driven move/resize remains in
  rawm because that mechanism depends directly on admitted client geometry and
  pointer grabs.

The standalone rawpanel implementation contains the native status worker and
providers. The duplicate rawm status implementation is transitional dead weight
once the external panel cutover is proven; it must be deleted rather than kept
as a compatibility fallback.

Session supervision is outside both rawm's runtime and repository authority.
The standalone `rawsession` process supervises desktop process topology; rawm
does not start or restart independent desktop sidecars.

## Dock/work-area boundary

Usable monitor geometry is window-manager authority, but panel geometry is not.
External desktop chrome publishes `_NET_WM_WINDOW_TYPE_DOCK` together with
`_NET_WM_STRUT_PARTIAL` (or legacy `_NET_WM_STRUT`). rawm observes those
standard reservations and derives its work areas from them. It does not require
a panel-specific command, socket, PID, or configuration reference.

This boundary is deliberately generic: rawm must react correctly to any
standards-conforming dock, whether or not it is rawpanel. Mapping, unmapping,
destroying, or changing a dock reservation recomputes work areas and rearranges
tiled clients. Full monitor geometry remains authoritative for fullscreen
clients.

## Private libraries

A library is introduced only when two components need one semantic authority.
It is not a destination for generic helpers merely because code can be shared.

`lib/status` owns only the private status snapshot semantics:

* state vocabulary;
* segment identifier grammar and byte limits;
* printable UTF-8 validation;
* snapshot encoding and decoding;
* the 4096-byte payload limit.

It does not own X11, rendering, scheduling, subprocess execution, configuration
policy, or presentation. The library is private, statically linked,
non-installed, and has no public ABI commitment.

Do not create catch-all libraries such as `librawmutil` or `librawmx11` without
a similarly specific authority boundary.

## External repository boundaries

`rawmenu` remains a separate repository. It has independent dmenu provenance,
independent release usefulness, and a Pinentry/secret-entry protocol and
security lifecycle that should not become part of the window-manager release
authority.

`rawclip` remains a separate repository. Clipboard capture, exclusion, history
retention, selection ownership, and rawmenu-backed history presentation form a
separate runtime authority. The current rawm tree may still launch `rawclip menu`
through transitional key policy; the target owner of that global key is
`rawkeys`. rawclip must not absorb global-key or window-manager policy.

`rawsession` is a separate repository. It owns desktop process topology: one
primary process, long-lived supervised sidecars, bounded restart policy, and
orderly shutdown. rawm is merely the stock primary and does not depend on
rawsession at build or runtime.

`rawlock` remains a separate repository. Screen locking and authentication form
a privileged security boundary with materially different failure, review, and
release requirements from the non-privileged desktop control plane. The current
rawm key policy may still invoke it during transition; the target invocation
owner is `rawkeys`. Neither rawm nor rawsession absorbs rawlock authentication
or long-lived lifecycle authority.

`rawin` and `rawkb` are migration sources, not long-term architecture. The
legacy in-tree `rawmctl` absorbed parts of both, but the replacement boundaries
are now explicit: standalone `rawctl` speaks only the rawm membrane,
`rawkeys` owns XKB/global-key policy, and `rawwin` owns the stateless interactive
window-selection composition. Historical repositories remain provenance rather
than compatibility ghosts.

## History and licensing

The rawmstat repository was merged without squashing. Its original head is a
real second parent of the import commit, so historical commit identities remain
reachable even though the current files were subsequently moved into the rawm
layout.

The current source tree carries the MIT/X license. The imported rawmstat history
remains reachable as provenance and its historical revisions retain their
original ISC notices. Current status files carry the current tree's MIT/X
licensing; see `LICENSES/README.md` for the provenance boundary.

## Change discipline

Cross-component changes follow authority rather than repository convenience:

1. define a producer-owned contract before extracting a consumer;
2. create an independent repository as soon as the extracted subsystem has its
   own authority, lifecycle, public interface, or failure domain;
3. delete extracted implementation from the source repository instead of
   keeping compatibility copies or dormant fallback authority;
4. keep producer contract tests with the producer and consumer behavior tests
   with the consumer; cross-repository composition belongs in the integration
   configuration repository;
5. introduce a shared library only after multiple independent consumers prove a
   stable reusable semantic boundary.

Repository integration is therefore a control mechanism, not an objective by
itself.
