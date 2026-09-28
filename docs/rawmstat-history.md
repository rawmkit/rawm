# rawmstat history and provenance

The standalone rawmstat project is historical provenance, not a current rawm
program or runtime authority.

Its complete pre-integration Git history was merged without squashing as the
second parent of the original import commit. Historical revisions therefore
remain reachable with their original commit identities and ISC notices.

Current rawm status code is an MIT/X implementation under `src/status/` and
`lib/status/`. Status configuration lives in `rawm.conf`; rawm forks and
supervises a private collector worker and transports snapshots over an anonymous
socket pair. No current source file, executable, manual page, configuration
file, or X11 status protocol depends on the historical rawmstat implementation.
