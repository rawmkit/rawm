# rawmstat history and provenance

rawmstat is the status acquisition sidecar shipped with rawm. The current
implementation uses named native sources, in-process sampling state, and the
`_RAWM_STATUS_V3` snapshot contract. It is implemented and licensed as part of
the current rawm source tree.

The repository also preserves the complete history of the earlier standalone
rawmstat project as the second parent of the import commit. That historical
project began as a fork of torrinfail's ISC-licensed dwmblocks at commit
`a933ce0` (2022-01-06) and later evolved asynchronous command blocks and the
rawm-v2 protocol before being integrated into rawm.

The native source implementation intentionally replaces that lineage in the
current tree. The historical commits remain reachable for provenance and keep
the license notices that applied to their revisions; they are not a current
implementation or compatibility authority.

Historical upstream references:

1. https://github.com/torrinfail/dwmblocks
2. https://github.com/torrinfail/dwmblocks/archive/a933ce0/dwmblocks-a933ce0.zip
