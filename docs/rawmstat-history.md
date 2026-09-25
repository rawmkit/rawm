# rawmstat history and provenance

`rawmstat` is the rawm status producer. It executes configured commands,
composes their first output lines, and publishes semantic segments through
rawm's `_RAWM_STATUS_V2` root-window property. The executable remains a
separate process even though it is built and released from the rawm source
repository.

The component began as a fork of torrinfail's `dwmblocks` at commit `a933ce0`
(2022-01-06). Before integration into the rawm repository it had diverged to
runtime libconfig policy, direct argv command execution, asynchronous monotonic
`poll(2)` scheduling, self-pipe signal handling, per-block timeouts, and the
rawm-v2 semantic status protocol.

The pre-integration rawmstat Git history is retained without squashing as a
second-parent history of the import commit. Historical commit identities are
therefore still present in this repository even though the current files were
subsequently moved under the rawm directory layout.

Original upstream:

1. https://github.com/torrinfail/dwmblocks
2. https://github.com/torrinfail/dwmblocks/archive/a933ce0/dwmblocks-a933ce0.zip

The imported component remains under the ISC license; see
`LICENSES/ISC.txt` and `LICENSES/README.md`.
