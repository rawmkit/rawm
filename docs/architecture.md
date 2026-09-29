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

* `rawmctl` currently mixes EWMH window operations, rawmenu composition, and XKB
  keyboard control. It is not the authority or specification for the new rawm
  membrane and must not be extracted verbatim. A future standalone `rawmctl`
  repository will be implemented against the rawm control/state contract only.
* session lifecycle has been extracted to the standalone `rawsession`
  repository; rawm no longer builds, installs, tests, or configures it.
* the built-in bar, systray, OSD, status providers, and ALSA control are future
  `rawpanel` authority. Their current residence in rawm is transitional.
* global keyboard grabs, command dispatch, and per-window XKB policy are future
  `rawkeys` authority. Pointer-driven move/resize remains in rawm because that
  mechanism depends directly on admitted client geometry and pointer grabs.

Status collection remains process-isolated while it is still resident in rawm.
The private worker is fault containment for the current implementation, not a
reason for status acquisition to remain part of the window-manager repository.
When `rawpanel` is extracted, the native status provider implementation moves
with it and is deleted from rawm.

Session supervision is outside both rawm's runtime and repository authority.
The standalone `rawsession` process supervises desktop process topology; rawm
does not start or restart independent desktop sidecars.

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
current in-tree `rawmctl` absorbed parts of both, but that mixed boundary is now
explicitly transitional: future `rawmctl` speaks only the rawm membrane, while
XKB/global-key authority belongs to `rawkeys`. Historical repositories should
remain provenance rather than compatibility ghosts.

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
