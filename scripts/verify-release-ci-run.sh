#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: verify-release-ci-run.sh <workflow-id> <commit-sha> <repository>" >&2
  exit 2
fi

workflow_id="$1"
expected_sha="$2"
expected_repo="$3"

if [[ ! "$workflow_id" =~ ^[0-9]+$ ]]; then
  echo "Expected a numeric workflow ID" >&2
  exit 2
fi

# A workflow's display name is user-editable and not unique. The immutable
# workflow_id ties the run to this repository's .github/workflows/ci.yml.
jq -e \
  --argjson workflow_id "$workflow_id" \
  --arg sha "$expected_sha" \
  --arg repo "$expected_repo" '
    .workflow_id == $workflow_id and
    .name == "CI" and
    .event == "push" and
    .head_branch == "master" and
    .head_sha == $sha and
    .head_repository.full_name == $repo
  ' >/dev/null
