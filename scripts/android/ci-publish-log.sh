#!/usr/bin/env bash
# CI helper: publishes a build log to the ci-logs/<name> branch of this
# repository (one orphan commit, force-pushed), so the errors of a run can be
# read with plain git. Needs GITHUB_TOKEN with contents: write.
set -uo pipefail
name="$1"
log="$2"
[[ -f "$log" ]] || { echo "No log at $log"; exit 0; }
work="$(mktemp -d)"
{
  echo "run: ${GITHUB_SERVER_URL:-}/${GITHUB_REPOSITORY:-}/actions/runs/${GITHUB_RUN_ID:-}"
  echo "commit: ${GITHUB_SHA:-}"
  echo "job: $name"
  echo
  echo "== errors (with context) =="
  grep -n -B2 -A6 -E "error:|FAILED:|CMake Error|fatal error|undefined (symbol|reference)|BUILD FAILED|\* What went wrong" "$log" | head -n 4000
  echo
  echo "== last 200 lines =="
  tail -n 200 "$log"
} > "$work/errors.txt"
gzip -c "$log" > "$work/full.log.gz"
cd "$work"
git init -q -b "ci-logs/$name"
git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git add errors.txt full.log.gz
git commit -q -m "CI log: $name (${GITHUB_RUN_ID:-local})"
git push -q -f "https://x-access-token:${GITHUB_TOKEN}@github.com/${GITHUB_REPOSITORY}.git" "HEAD:refs/heads/ci-logs/$name" || echo "Could not publish the log"
