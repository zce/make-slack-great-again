#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
guard="$root/scripts/verify-release-ci-run.sh"
repo="zce/make-slack-great-again"
sha="0123456789abcdef0123456789abcdef01234567"
workflow_id=12345

run_json() {
  jq -cn \
    --argjson workflow_id "$1" \
    --arg name "$2" \
    --arg event "$3" \
    --arg branch "$4" \
    --arg sha "$5" \
    --arg repo "$6" '
      {
        workflow_id: $workflow_id,
        name: $name,
        event: $event,
        head_branch: $branch,
        head_sha: $sha,
        head_repository: {full_name: $repo}
      }
    '
}

expect_rejected() {
  local label="$1" json="$2"
  if printf '%s\n' "$json" | bash "$guard" "$workflow_id" "$sha" "$repo"; then
    echo "Unexpectedly accepted: $label" >&2
    exit 1
  fi
}

# The exact CI workflow and commit are accepted.
run_json "$workflow_id" CI push master "$sha" "$repo" |
  bash "$guard" "$workflow_id" "$sha" "$repo"

# A different workflow with the same display name must not authorize release.
expect_rejected "same name, different workflow ID" \
  "$(run_json 99999 CI push master "$sha" "$repo")"
expect_rejected "different commit" \
  "$(run_json "$workflow_id" CI push master "ffffffffffffffffffffffffffffffffffffffff" "$repo")"
expect_rejected "pull request event" \
  "$(run_json "$workflow_id" CI pull_request master "$sha" "$repo")"
expect_rejected "non-master branch" \
  "$(run_json "$workflow_id" CI push feature/test "$sha" "$repo")"
expect_rejected "different repository" \
  "$(run_json "$workflow_id" CI push master "$sha" "someone-else/repo")"
expect_rejected "different workflow name" \
  "$(run_json "$workflow_id" Build push master "$sha" "$repo")"

echo "Release CI provenance checks passed."
