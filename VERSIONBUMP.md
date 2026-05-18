# Release Tooling

## Bumping the release tag

The `bump_version` custom target computes the next semver tag from the highest
existing `vX.Y.Z` git tag and (optionally) creates and pushes it.

### Usage

Dry-run (prints the `git tag && git push` it would run, makes no changes):

```sh
pio run -t bump_version
```

The same logic is also runnable as a plain script:

```sh
python scripts/bump_version.py            # dry-run patch
python scripts/bump_version.py minor      # dry-run minor
python scripts/bump_version.py patch --execute   # actually tag and push
```

### Environment variables

The `pio run -t bump_version` target is driven by environment variables:

| Variable       | Default  | Effect                                                                            |
| -------------- | -------- | --------------------------------------------------------------------------------- |
| `BUMP_LEVEL`   | `patch`  | Which semver component to bump. One of `major`, `minor`, `patch`.                 |
| `BUMP_EXECUTE` | _unset_  | Truthy (`1`, `true`, `yes`, `on`) runs `git tag` + `git push`. Otherwise dry-run. |
| `BUMP_REMOTE`  | `origin` | Remote to push the new tag to.                                                    |

Examples:

```sh
# Dry-run a minor bump
BUMP_LEVEL=minor pio run -t bump_version

# Actually cut a patch release and push to origin
BUMP_EXECUTE=1 pio run -t bump_version

# Cut a major release and push to a non-default remote
BUMP_LEVEL=major BUMP_EXECUTE=1 BUMP_REMOTE=upstream pio run -t bump_version
```

### Behavior

- The next tag is computed from the highest existing `vMAJOR.MINOR.PATCH` git
  tag in the local repo. Pre-releases / non-semver tags are ignored.
- If the computed tag already exists, the command exits non-zero without
  touching git state.
- An invalid `BUMP_LEVEL` exits non-zero with an explanatory message before any
  git command runs.
- Dry-run prints the exact `git tag … && git push …` command and exits 0; no
  git side effects.
