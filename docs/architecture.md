# rawm repository architecture

## Repository authority

The rawm repository owns the non-privileged X11 desktop control plane. A
repository boundary is chosen for shared protocol/release authority, not to
force components into one process.

Current programs:

* `rawm`: window-manager policy, client admission, geometry, focus, bar,
  compositor-facing state, and the private status-worker lifecycle.
* `rawmctl`: direct operational control and inspection of rawm/X11 state.
  Window list/activation use EWMH authority, window selection composes with
  `rawmenu` while retaining XID identity, and keyboard-group control uses XKB.
  These behaviors replace the useful portions of the historical `rawin` and
  `rawkb` utilities without importing their external-tool chains.
* `rawmsession`: session-lifecycle authority. It starts rawm as the primary
  process and supervises independent long-lived desktop infrastructure such as
  Picom. The primary defines session lifetime; sidecars may be restarted under
  a bounded crash budget.

Status collection is process-isolated but not a separate product. rawm forks a
private worker after validating `rawm.conf`; the worker owns acquisition state
and communicates only with its parent through an anonymous packet socket.
Provider failures therefore remain outside the X11 window-manager process while
configuration and lifecycle authority remain inside rawm.

Session supervision is likewise kept out of rawm. rawm owns window-manager
state and already performs an in-place SIGHUP restart. Starting independent desktop sidecars from
the WM would make that restart path duplicate-prone and would couple compositor
failures to the X11 policy engine. rawmsession therefore owns those process
trees one layer above rawm. The status worker is different: it is an internal
fault-containment process whose lifetime is part of rawm itself.

The session boundary is intentionally narrower than a generic service manager.
Only continuously desired desktop infrastructure belongs in `sidecars`; user
applications, gpg-agent/Pinentry, screen locking, and one-shot startup actions
retain their own lifecycle authorities.

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
separate runtime authority. rawm may invoke `rawclip menu` through an ordinary
configured argv binding and owns the global key grab, but rawclip must not grab
global keys or move clipboard-history policy into the window manager.

`rawlock` remains a separate repository. Screen locking and authentication form
a privileged security boundary with materially different failure, review, and
release requirements from the non-privileged desktop control plane. rawm may
invoke it through an ordinary configured argv binding, but does not absorb its
authentication policy, and rawmsession must not supervise it as a long-lived
sidecar.

`rawin` and `rawkb` are migration sources, not long-term architecture. Their
useful behaviors are now implemented by `rawmctl` using explicit EWMH and XKB
authorities. After release acceptance, their original repositories should be
retained read-only for provenance rather than kept as compatibility ghosts.

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
