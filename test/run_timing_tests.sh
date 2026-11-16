#!/usr/bin/env bash
# Runs the unit tests tagged [timing] one at a time, retrying each that fails.
# Usage: run_timing_tests.sh <unit_test binary>
#
# These tests run against the mock's real-time threads (the spindle's encoder,
# the motors), so a busy machine can fail them now and then: see "Unit tests
# which fail now and then" in TODO.md. A test only counts as failed if it fails
# every try. One that passes on a retry is listed as flaky, as it's worth
# knowing if a test starts doing that often.

set -u
binary="$1"
tries=3

flaky=()
failed=()
output=$(mktemp)
trap 'rm -f "$output"' EXIT

while IFS= read -r name; do
    [ -z "$name" ] && continue
    # (A test spec treats commas, brackets, asterisks and backslashes
    # specially, so they're escaped)
    spec=$(printf '%s' "$name" | sed 's/[][,*\\]/\\&/g')
    start=$SECONDS
    passed=0
    for try in $(seq 1 $tries); do
        # (A spec that matches nothing still exits 0)
        if "$binary" "$spec" > "$output" 2>&1 && ! grep -q "^No tests ran" "$output"; then
            passed=$try
            break
        fi
    done
    elapsed=$((SECONDS - start))
    if [ "$passed" -eq 1 ]; then
        echo "  ok      ${elapsed}s  $name"
    elif [ "$passed" -gt 1 ]; then
        echo "  flaky   ${elapsed}s  $name (passed on try $passed)"
        flaky+=("$name")
    else
        echo "  FAILED  ${elapsed}s  $name (every try)"
        cat "$output"
        failed+=("$name")
    fi
done < <("$binary" --list-test-names-only "[timing]")

echo "[timing] tests: ${#failed[@]} failed, ${#flaky[@]} flaky (passed on a retry)"
[ ${#failed[@]} -eq 0 ]
