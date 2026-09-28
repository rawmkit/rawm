# Status architecture

Status is a rawm subsystem with two process-local authorities.

The rawm process owns configuration, worker lifecycle, presentation, and
interaction. It parses `rawm.conf`, validates the complete status policy,
starts the collector worker, renders semantic segments, performs hit testing,
and resolves mouse bindings by stable segment identifier.

The collector worker owns acquisition state. It samples native system sources,
keeps baselines and freshness state in memory, and sends atomic snapshots to
its parent. The worker is deliberately a separate process so provider or
extension failures do not execute inside the X11 window-manager process.

The worker is not a public program. It has no executable name, configuration
file, PID file, service entry, X11 property, or rendezvous path.

## Configuration authority

`rawm.conf` is the only configuration authority. Status source policy lives
under `status.sources`. rawm parses and validates it before the worker is
created. The forked worker inherits that canonical in-memory configuration; it
does not parse libconfig and cannot observe a different configuration view.

Named source groups compose across rawm's normal system/user configuration
layers. An existing source may be overridden without repeating its provider,
and `enabled = false` disables an inherited source explicitly.

A SIGHUP uses rawm's existing in-place restart boundary. The replacement rawm
process validates the new complete configuration before creating its own
worker, so there is never a second independent status reload authority.

## Private transport

rawm creates an anonymous `AF_UNIX` `SOCK_SEQPACKET` socket pair. One endpoint
remains in rawm and the other is inherited by the worker. The pair provides
message boundaries and worker-lifetime detection without a filesystem socket
or X11-global state.

Each packet is one private status-snapshot frame. The payload contains zero or
more newline-terminated records:

```
<id>\t<state>\t<text>\n
```

`id` is a stable 1..64 byte identifier using `[A-Za-z0-9._-]`. `text` is
non-empty printable UTF-8. Duplicate identifiers, control bytes, malformed
UTF-8, unknown states, and payloads larger than 4096 bytes invalidate the
snapshot as a unit.

The states are:

* `normal`: a current measurement in its normal range;
* `warning`: a current measurement in its warning range;
* `critical`: a current measurement in its critical range;
* `stale`: the source has a previously valid value, but its latest refresh
  failed or the worker itself has become unavailable.

The snapshot codec is private implementation, not a desktop protocol. It has no
public version selector or compatibility parser.

## Source model

A source is a named measurement with a provider and provider-specific policy.
Native providers keep their sampling state in the worker. Periodic sources use
a monotonic clock. Event-capable providers contribute descriptors directly to
the worker poll set and therefore share the same lifecycle and wake-up authority
as the private parent socket.

Current native providers are ALSA playback volume, battery, clock, CPU,
filesystem, memory, and network. The ALSA provider opens one libasound mixer,
reads a named playback control, and remains event-driven while healthy; its
configured period is used only to retry an unavailable mixer/control. `exec` is
an explicit extension boundary for measurements without a native provider.
Command execution is therefore one provider, not the status architecture
itself.

A failed source with no previous value is omitted. A failed refresh after a
valid sample preserves the text and marks the segment `stale`. A successful
refresh replaces both text and semantic state.

## Worker lifecycle

rawm owns the worker as a child process. Unexpected worker death does not end
the window manager. Existing segments become stale and rawm restarts the worker
with bounded backoff. A worker that remains alive for ten seconds resets the
backoff; receiving an immediate snapshot is deliberately not enough to classify
a rapidly crashing worker as healthy.

Normal rawm shutdown terminates and reaps the worker. No external session
supervisor should start or monitor a status process.

## Historical boundary

The former standalone rawmstat repository and its earlier dwmblocks-derived
block/signal architecture remain reachable in Git history for provenance. They
are not runtime, configuration, transport, or implementation authorities in
the current tree.
