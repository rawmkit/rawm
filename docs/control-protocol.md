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
  authoritative state snapshot.

A consumer that needs change notification selects `PropertyChangeMask` on the
owner and rereads `_RAWM_STATE` after a `PropertyNotify`. rawm replaces the
property only when the serialized authoritative state changes. Consumers must
always treat the property as a complete snapshot rather than an incremental
event stream.

## State version 1

The first line is exactly:

```
rawm-state-v1
```

Consumers must reject snapshots with another version marker. Records are
newline-terminated and fields are separated by a single horizontal tab. Within
version 1, an unknown record kind or an incorrect field count is malformed and
must be rejected rather than silently ignored.

The snapshot starts with:

```
tag-count<TAB>N
selected-monitor<TAB>N
```

`selected-monitor` is `-1` only when no monitor authority exists.

Each admitted monitor then contributes one `monitor` record followed by one
`tag` record for every configured tag. The monitor record begins with the
literal `monitor`; all remaining fields below are tab-separated in the listed
order. A tag record is `tag`, monitor number, one-based tag index, and name.

Monitor fields are:

* `number`: rawm monitor number.
* `selected`: `0` or `1`.
* `mx`, `my`, `mw`, `mh`: full monitor geometry in X coordinates.
* `viewed`, `occupied`, `urgent`: eight-digit hexadecimal tag masks without a
  prefix.
* `layout`: one of `tile`, `floating`, `monocle`, `bstack`,
  `bstack_horizontal`, or `grid`.
* `focused`: focused client XID as `0x...`, or `0x0`.
* `nmaster`: current master-client count.
* `mfact`: current master factor with six decimal places.

Tag `index` is one-based. Tag names are UTF-8. Literal percent, tab, newline,
and carriage-return bytes are encoded as `%25`, `%09`, `%0A`, and `%0D`
respectively. Consumers decode only `%HH` escapes and otherwise preserve UTF-8
bytes exactly.

The snapshot deliberately does not publish panel presentation, status values,
keyboard state, or process lifecycle. Those are different authorities.

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
