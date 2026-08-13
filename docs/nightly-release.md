# Nightly release policy

The `nightly` channel is a pointer, not a mutable toolchain release. Each
successful attempt creates an immutable prerelease named `nightly-<40-character
source SHA>-<workflow run id>-<attempt>`. Its package version contains the UTC
date, full source SHA, run id and attempt, so a corrected manual retry never
mutates or collides with an earlier candidate. The
installed `share/janus/janus-source-sha` file and `janusup`'s `.janus-version`
metadata retain the full SHA.

Publication has one commit point: a compare-and-swap update of the dedicated
`nightly-channel` Git ref. Its tree contains only `version`, so readers observe
either the complete old manifest or the complete new one; unlike release asset
`--clobber`, there is no delete-before-upload gap. Before that update, every
Linux, macOS and
Windows archive must build, pass tests and package smoke tests; checksums and
GitHub attestations must verify; and the Linux candidate must pass an end-to-end
`janusup install nightly` plus the pinned Janus8 downstream build. A failure at
any earlier gate leaves the prior channel manifest untouched and installable.
Candidate prereleases that fail validation are never channel-visible.

The enriched manifest is four whitespace-separated fields:

```text
<package-version> <immutable-release> <full-source-sha> <published-at-UTC>
```

`janusup` also accepts the historical two-field stable/beta format, but rejects
partial SHA values, extra fields and trailing garbage.

## Freshness, retention and rollback

Freshness is unhealthy when the nightly publication time precedes stable or is
more than seven days old. The deterministic check in
`scripts/nightly_release.py` is intended for scheduled monitoring as well as
local tests.

Keep the current channel target and the newest seven immutable snapshots (and
at least 30 days when seven snapshots are newer than that). A maintainer may
delete older unreferenced prereleases after checking that no channel manifest
names them. Failed candidates may be removed immediately after diagnosis.

Rollback never rebuilds or edits a snapshot: download and validate the desired
immutable release, then atomically commit that snapshot's original four-field
manifest to the `nightly-channel` ref. Preserve the source SHA and
publication timestamp. Re-run checksum, attestation, `janusup`, identity and
Janus8 gates before changing the pointer. If rollback validation fails, leave
the existing manifest unchanged.
