# rawm repository architecture

## Repository authority

The rawm repository owns the non-privileged X11 desktop control plane. A
repository boundary is chosen for shared protocol/release authority, not to
force components into one process.

Current programs:

* `rawm`: window-manager policy, client admission, geometry, focus, bar,
  compositor-facing state, and rawm-owned X11 properties.
* `rawmstat`: asynchronous status production and publication through the
  rawm-v2 contract.

Planned program:

* `rawmctl`: direct operational control and inspection of rawm/X11 state. It is
  intended to replace the historical `rawin` and `rawkb` utility repositories
  after behavioral parity is covered by tests.

`rawmstat` remains a separate process. Integrating its source and release
authority does not make status command execution part of the window-manager
process or failure domain.

## Private libraries

A library is introduced only when two components need one semantic authority.
It is not a destination for generic helpers merely because code can be shared.

`lib/status` owns only the rawm-v2 transport contract:

* state vocabulary;
* segment identifier grammar and byte limits;
* printable UTF-8 validation;
* wire encoding and decoding;
* the 4096-byte payload limit.

It does not own X11 property access, rendering, status scheduling, subprocess
execution, configuration policy, or presentation. The library is private,
statically linked, non-installed, and has no public ABI commitment.

Do not create catch-all libraries such as `librawmutil` or `librawmx11` without
a similarly specific authority boundary.

## External repository boundaries

`rawmenu` remains a separate repository. It has independent dmenu provenance,
independent release usefulness, and a Pinentry/secret-entry protocol and
security lifecycle that should not become part of the window-manager release
authority.

`rawlock` remains a separate repository. Screen locking and authentication form
a privileged security boundary with materially different failure, review, and
release requirements from the non-privileged desktop control plane.

`rawin` and `rawkb` are migration sources, not long-term architecture. Their
small behaviors should be reimplemented under `rawmctl` using explicit EWMH,
XKB, and future rawm command contracts. Their original repositories should then
be retained read-only for provenance rather than kept as compatibility ghosts.

## History and licensing

The rawmstat repository was merged without squashing. Its original head is a
real second parent of the import commit, so historical commit identities remain
reachable even though the current files were subsequently moved into the rawm
layout.

Moving source into one repository does not erase component licensing. rawm's
window-manager sources remain MIT/X; imported rawmstat sources remain ISC; the
shared status codec is distributed under the combined `MIT AND ISC` terms. See
`LICENSES/README.md`.

## Change discipline

Cross-component changes should follow the authority boundary:

1. change a private semantic library when the contract itself changes;
2. change each producer/consumer in separate commits when their behavior must
   adapt to that contract;
3. keep integration tests in the same repository so incompatible producer and
   consumer states cannot be released accidentally;
4. keep externally privileged or independently reusable components in their
   own repositories even when the desktop session composes them.

Repository integration is therefore a control mechanism, not an objective by
itself.
