#!/usr/bin/env bash
# Resync the local musescore5-custom branch with upstream musescore/MuseScore main.
#
# Usage: ./resync-custom-branch.sh
#
# - Fetches the official "origin" remote (repo + "muse" submodule).
# - Merges origin/main into musescore5-custom (--no-ff, one commit per resync).
# - On a clean merge: updates submodules and reminds you to rebuild.
# - On conflicts: leaves the repo in the conflicted state and prints what to
#   resolve. A conflict on "muse" itself means the submodule's history diverged
#   (e.g. a custom commit not yet upstream) and needs a manual merge inside
#   muse/ before "git add muse" in the superproject — see
#   project_musescore5_custom_branch.md in Claude's memory for the steps used
#   the first time this came up.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

CUSTOM_BRANCH="musescore5-custom"
UPSTREAM_REMOTE="origin"
UPSTREAM_BRANCH="main"

if ! git rev-parse --verify "$CUSTOM_BRANCH" >/dev/null 2>&1; then
    echo "ERROR: branch '$CUSTOM_BRANCH' does not exist." >&2
    exit 1
fi

echo "==> Checking working tree is clean"
if [[ -n "$(git status --porcelain)" ]]; then
    echo "ERROR: working tree is not clean. Commit or stash your changes first." >&2
    git status --short
    exit 1
fi

echo "==> Fetching $UPSTREAM_REMOTE"
git fetch "$UPSTREAM_REMOTE"

echo "==> Fetching submodule 'muse' remotes"
git -C muse fetch --all

echo "==> Switching to $CUSTOM_BRANCH"
git checkout "$CUSTOM_BRANCH"

BEHIND=$(git rev-list --count "HEAD..${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}")
if [[ "$BEHIND" -eq 0 ]]; then
    echo "==> Already up to date with ${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}. Nothing to do."
    exit 0
fi

echo "==> $BEHIND new commit(s) on ${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}. Merging..."
if git merge --no-ff "${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}" -m "Resync ${CUSTOM_BRANCH} with ${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}"; then
    echo "==> Merge succeeded cleanly."
    git submodule update --init --recursive
    echo "==> Done. Run a full 'ninja mscore' rebuild before testing."
    exit 0
fi

echo ""
echo "==> Merge produced conflicts. Conflicted paths:"
CONFLICTS="$(git diff --diff-filter=U --name-only)"
echo "$CONFLICTS"
echo ""
if grep -qx "muse" <<<"$CONFLICTS"; then
    echo "NOTE: 'muse' (submodule) conflicted — its history diverged from upstream's."
    echo "Resolve it manually: merge the two submodule commits inside muse/, then"
    echo "'git add muse' in the superproject before continuing."
    echo ""
fi
echo "Once everything is resolved: git add <files> && git commit --no-edit"
exit 1
