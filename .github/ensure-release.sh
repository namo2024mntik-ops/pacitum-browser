#!/usr/bin/env bash
# Tag builds: make sure the GitHub release for this tag exists before uploading files to it.
# A missing release is created from release-notes/<tag>.md (published, not a draft). The legacy
# 1.8.x line is never marked "Latest", so /releases/latest keeps pointing at the newest 1.9.x.
# Exit 1 when there is no release and no notes file (the caller then skips the upload).
set -u
tag="$1"; repo="$2"
gh release view "$tag" --repo "$repo" >/dev/null 2>&1 && exit 0
notes="release-notes/$tag.md"
[ -f "$notes" ] || exit 1
title=$(head -n 1 "$notes" | sed 's/^# *//')
latest="--latest"
case "$tag" in v1.8.*) latest="--latest=false" ;; esac
# Several jobs (Windows, Linux, macOS) may try at once: a failed create is fine if the release now exists.
tail -n +2 "$notes" > "$RUNNER_TEMP/notes-$tag.md"
gh release create "$tag" --repo "$repo" --verify-tag --title "$title" --notes-file "$RUNNER_TEMP/notes-$tag.md" $latest \
  || { sleep 5; gh release view "$tag" --repo "$repo" >/dev/null 2>&1; }
