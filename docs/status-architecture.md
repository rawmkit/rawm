# Status architecture

The rawm status path has two authorities.

`rawmstat` owns acquisition and publication. It samples native system sources,
maintains sampling state, assigns semantic health/freshness state, and publishes
one atomic snapshot on the X11 root window.

`rawm` owns presentation and interaction. It parses the snapshot, lays out the
bar, selects colours, performs segment hit testing, and resolves configured
mouse bindings by stable segment identifier.

The two processes communicate only through `_RAWM_STATUS_V3`, a UTF-8 root
window property. Neither process discovers, signals, or supervises the other.
`rawmsession` owns sidecar lifetime.

## Source model

A status source is a named producer with a provider and provider-specific
configuration. Native providers keep their state in the long-running
`rawmstat` process. Periodic sources are scheduled from a monotonic clock.
Providers may later expose event file descriptors without changing the public
snapshot contract.

Command execution is not the status model. The `exec` provider is an extension
boundary for measurements that do not yet have a native provider. It is
bounded by a timeout and has no realtime-signal control API.

## Snapshot contract

`_RAWM_STATUS_V3` contains zero or more newline-terminated records:

```
<id>\t<state>\t<text>\n
```

`id` is a stable 1..64 byte identifier using `[A-Za-z0-9._-]`. `text` is
non-empty printable UTF-8. Duplicate identifiers, embedded control bytes,
malformed UTF-8, unknown states, and payloads larger than 4096 bytes are
invalid. A consumer rejects an invalid snapshot as a unit.

The states are:

* `normal`: a current measurement in its normal range;
* `warning`: a current measurement in its warning range;
* `critical`: a current measurement in its critical range;
* `stale`: the source has a previously valid value, but its latest refresh
  failed.

A failed source with no previous value is omitted. A successful refresh clears
`stale` and restores the provider's current semantic state.

The wire version is not configurable. rawm and rawmstat are released together;
a protocol change therefore changes the property name and codec rather than
adding compatibility policy to user configuration.

## Compatibility boundary

The former rawm-v2 block model, `interval`, realtime `signal` offsets, and the
public block command scheduler are retired. `_RAWM_STATUS_V2` is not consumed
or produced by current code. There is no compatibility parser or translation
path.

Historical rawmstat/dwmblocks ancestry remains available in Git history. It is
not an implementation authority for the current status subsystem.
