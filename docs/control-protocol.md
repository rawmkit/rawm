# rawm control and state protocol

## Purpose

The control protocol is the public membrane around rawm's window-management
authority. External components may command rawm through this interface and may
observe state published by rawm, but must not reconstruct rawm semantics by
walking the X window tree, interpreting private client properties, or combining
EWMH observations into guessed tag/layout state.

The protocol intentionally contains only window-manager authority. Process
execution, panel visibility, status collection, volume control, global keyboard
policy, and XKB policy are outside this contract.

## Discovery and liveness

For X screen `N`, rawm owns the selection:

```
_RAWM_CONTROL_SN
```

For the usual screen zero this is `_RAWM_CONTROL_S0`.

The selection owner is an unmapped InputOnly window created by rawm. Protocol
properties live on that owner window and control `ClientMessage` events are sent
to it. Consumers discover the current authority with `XGetSelectionOwner()`.

The owner window is also the liveness boundary. If rawm exits or loses its X
connection, X destroys the window and clears the selection. Consumers must not
cache the owner XID across selection-owner loss.

## Owner properties

The owner publishes:

* `_RAWM_CONTROL_VERSION`, type `CARDINAL`, format 32. Version 1 is defined by
  this document.
* `_RAWM_SUPPORTED_COMMANDS`, type `ATOM`, format 32. Every listed atom is a
  command accepted by this rawm instance.
* `_RAWM_STATE`, type `UTF8_STRING`, format 8. This is the complete current
  authoritative monitor/tag/layout state snapshot.
* `_RAWM_CLIENTS`, type `UTF8_STRING`, format 8. This is the complete current
  authoritative admitted-client inventory.
* `_RAWM_FEEDBACK`, type `UTF8_STRING`, format 8. This is the latest transient
  semantic feedback event for presentation consumers. It is advisory, not
  authoritative state.

A consumer that needs change notification selects `PropertyChangeMask` on the
owner and rereads `_RAWM_STATE` after a `PropertyNotify`. rawm replaces the
property only when the serialized authoritative state changes. Consumers must
always treat the property as a complete snapshot rather than an incremental
event stream.

## State version 2

The first line is exactly:

```
rawm-state-v2
```

Consumers must reject snapshots with another version marker. Records are
newline-terminated and fields are separated by a single horizontal tab. Within
version 2, an unknown record kind or an incorrect field count is malformed and
must be rejected rather than silently ignored.

The snapshot starts with:

```
tag-count<TAB>N
selected-monitor<TAB>N
```

`selected-monitor` is `-1` only when no monitor authority exists.

Each admitted monitor then contributes one `monitor` record, one `stack`
record, and one `tag` record for every configured tag. The monitor record begins
with the literal `monitor`; all remaining fields below are tab-separated in the
listed order. A tag record is `tag`, monitor number, one-based tag index, and
name.

Monitor fields are:

* `number`: rawm monitor number.
* `selected`: `0` or `1`.
* `mx`, `my`, `mw`, `mh`: full monitor geometry in X coordinates.
* `wx`, `wy`, `ww`, `wh`: rawm-authoritative work-area geometry after external
  dock/strut reservations. Consumers that place desktop presentation surfaces
  must use this geometry rather than reconstructing work areas from EWMH.
* `viewed`, `occupied`, `urgent`: eight-digit hexadecimal tag masks without a
  prefix.
* `layout`: one of `tile`, `floating`, `monocle`, `bstack`,
  `bstack_horizontal`, or `grid`.
* `focused`: focused client XID as `0x...`, or `0x0`.
* `nmaster`: current master-client count.
* `mfact`: current master factor with six decimal places.

The stack record is:

```
stack<TAB>MONITOR<TAB>FOCUSED-INDEX<TAB>VISIBLE-COUNT
```

`FOCUSED-INDEX` is the one-based index of the monitor's focused client among
currently visible clients, or `0` when no visible client is focused.
`VISIBLE-COUNT` is the number of visible admitted clients on that monitor. The
record is observational state published by rawm; consumers must not infer rawm's
full stacking history from these two counts.

Tag `index` is one-based. Tag names are UTF-8. Literal percent, tab, newline,
and carriage-return bytes are encoded as `%25`, `%09`, `%0A`, and `%0D`
respectively. Consumers decode only `%HH` escapes and otherwise preserve UTF-8
bytes exactly.

The snapshot deliberately does not publish panel presentation, status values,
keyboard state, or process lifecycle. Those are different authorities.


## Client inventory version 1

The first line of `_RAWM_CLIENTS` is exactly:

```
rawm-clients-v1
```

Every remaining line is a `client` record with tab-separated fields:

```
client<TAB>XID<TAB>MONITOR<TAB>TAGS<TAB>VISIBLE<TAB>FOCUSED<TAB>CLASS<TAB>TITLE
```

`XID` is hexadecimal with a `0x` prefix. `MONITOR` is rawm's admitted monitor
number. `TAGS` is an eight-digit hexadecimal tag mask without a prefix.
`VISIBLE` and `FOCUSED` are `0` or `1`; `FOCUSED` names the globally focused
client, not merely a monitor-local stack selection. `CLASS` is the admitted
client's current `WM_CLASS` class component and `TITLE` is rawm's current
window-title observation. Text fields use the same `%HH` escaping as tag names.

This inventory is the authority boundary for managed-window enumeration and for
rawm's admitted client metadata observation. rawm republishes the inventory when
an admitted client's title or `WM_CLASS` class component changes, even when no
monitor/tag/layout state changes. Consumers that present those fields must read
them from `_RAWM_CLIENTS` rather than independently observing client properties.
Consumers must not reconstruct rawm's admitted client set from `_NET_CLIENT_LIST`,
the X window tree, or a combination of EWMH properties.

## Transient feedback version 1

`_RAWM_FEEDBACK` is an event-oriented presentation input. rawm replaces the
property for every feedback event, including repetitions of the same semantic
event. Consumers select `PropertyChangeMask` on the control owner and reread the
property after notification. Missing events are acceptable; consumers must use
`_RAWM_STATE` and `_RAWM_CLIENTS` whenever correctness depends on current WM
truth. For events whose presentation depends on tag labels or the selected view,
rawm publishes the corresponding authoritative snapshot before the feedback
record.

A version-1 record is one UTF-8 line with tab-separated fields:

```
rawm-feedback-v1<TAB>SERIAL<TAB>EVENT<TAB>MONITOR<TAB>ARG0<TAB>ARG1
```

`SERIAL` is a monotonically increasing unsigned process-local event number.
`MONITOR` is the rawm monitor number. `ARG0` and `ARG1` are signed decimal
semantic arguments. Version 1 defines these events:

* `monitor`: selected monitor changed; `ARG0` is the selected monitor number.
* `master-count`: `ARG0` is the current master-client count.
* `master-factor`: `ARG0` is the current master factor scaled by 1,000,000.
* `move`: `ARG0`,`ARG1` are client x/y relative to the monitor work area.
* `resize`: `ARG0`,`ARG1` are client width/height.
* `view`: `ARG0` is the current viewed tag mask. Consumers may reread state for
  tag names and the canonical layout name.
* `layout`: `ARG0` is the layout ID using the command protocol's layout IDs.
* `floating`: `ARG0` is 0 or 1 for the selected client's resulting elevated
  state.
* `fullscreen`: `ARG0` is 0 or 1 for the selected client's resulting state.
* `tag`: `ARG0` is the selected client's assigned tag mask.
* `toggle-tag`: `ARG0` is the selected client's resulting tag mask.
* `tag-monitor`: `ARG0` is the destination monitor number.

Formatting, fonts, colors, visibility duration, placement, and window lifecycle
are deliberately absent from this contract. Those belong to the presentation
consumer.

## Commands

Commands are X `ClientMessage` events sent directly to the current control
selection owner. Clients use `XSendEvent()` with propagation disabled and
`NoEventMask`; the owner window itself is the destination authority. The event
has:

```
message_type = _RAWM_COMMAND
format       = 32
data.l[0]    = 1                    # protocol version
data.l[1]    = command atom
data.l[2]    = arg0
data.l[3]    = arg1
data.l[4]    = requestor XID
```

Unused fields are zero. Integer arguments are interpreted as signed 32-bit
values. `requestor` is used only by commands with an external text payload.
Invalid versions, unsupported command atoms, and invalid arguments are ignored.
There is no reply message: command capability is discovered from
`_RAWM_SUPPORTED_COMMANDS`, while resulting admitted state is observed through
`_RAWM_STATE`.

### Command atoms

`_RAWM_CMD_FOCUS_STACK`
: `arg0` is `-1` for previous or `1` for next visible client.

`_RAWM_CMD_FOCUS_INDEX`
: `arg0` is a positive one-based visible-client index.

`_RAWM_CMD_FOCUS_CLIENT`
: `arg0` carries the target admitted client XID as an unsigned 32-bit value.
  rawm ignores XIDs that are no longer admitted. If the target is outside its
  monitor's current view, rawm switches that monitor to the client's admitted
  tag set before applying the normal focus/pop semantics.

`_RAWM_CMD_INC_MASTER`
: `arg0` is a non-zero signed delta.

`_RAWM_CMD_SET_MASTER_FACTOR`
: `arg0` is a factor scaled by 1,000,000. `arg1 = 0` means a signed delta;
  `arg1 = 1` means an absolute factor. Absolute factors outside 0.1 through 0.9
  are rejected.

`_RAWM_CMD_ZOOM`
: Promote the selected tiled client using rawm's existing zoom semantics.

`_RAWM_CMD_VIEW`
: `arg0 = -1` selects all tags, `arg0 = 0` selects the previous view, and a
  positive value selects that one-based tag.

`_RAWM_CMD_TOGGLE_VIEW`
: `arg0 = -1` or a positive one-based tag selector.

`_RAWM_CMD_CYCLE_VIEW`
: `arg0` is `-1` or `1`.

`_RAWM_CMD_KILL_CLIENT`
: Request close/termination of the selected client using rawm's normal client
  close semantics.

`_RAWM_CMD_SET_LAYOUT`
: `arg0 = -1` toggles the current/previous layout. Otherwise layout IDs are:
  `0 = tile`, `1 = floating`, `2 = monocle`, `3 = bstack`,
  `4 = bstack_horizontal`, `5 = grid`.

`_RAWM_CMD_CYCLE_LAYOUT`
: `arg0` is `-1` or `1`.

`_RAWM_CMD_TOGGLE_FLOATING`
: Toggle explicit floating state of the selected client.

`_RAWM_CMD_TAG`
: `arg0 = -1` assigns all tags; a positive value assigns that one-based tag.

`_RAWM_CMD_TOGGLE_TAG`
: `arg0 = -1` or a positive one-based tag selector.

`_RAWM_CMD_WINDOW_VIEW`
: Switch to the selected client's tag set using rawm's current window-view
  semantics.

`_RAWM_CMD_TOGGLE_FULLSCREEN`
: Toggle fullscreen state of the selected client.

`_RAWM_CMD_FOCUS_MONITOR`
: `arg0` is `-1` or `1`.

`_RAWM_CMD_TAG_MONITOR`
: Send the selected client to the monitor selected by direction `-1` or `1`.

`_RAWM_CMD_RENAME_TAG`
: `data.l[4]` names a requestor window. Before sending the command, the client
  stores the requested UTF-8 name in `_RAWM_COMMAND_TEXT` on that window. rawm
  consumes and deletes the property. The payload is at most
  `RAWM_TAG_NAME_MAX` bytes; an empty payload restores numeric names for the
  viewed tags.

`_RAWM_CMD_QUIT`
: `arg0 = 0` exits rawm and `arg0 = 1` requests the existing in-place restart
  path.

## Explicit non-authority

Version 1 intentionally has no command for:

* spawning arbitrary programs;
* showing or hiding a panel;
* status collection or status refresh;
* volume control;
* global key bindings or XKB groups;
* pointer-driven move/resize gestures.

The first five belong to other control systems. Pointer move/resize remains a
rawm window-manipulation mechanism because it depends on admitted client
geometry and an active pointer grab; it is not global keyboard policy.
