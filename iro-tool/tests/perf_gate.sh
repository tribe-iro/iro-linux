#!/usr/bin/env bash
set -euo pipefail

MODE="check"
BUILD_DIR="iro-tool/build-gcc-update"
RUNS=7
TEST_REGEX="integration_dwarf_fixtures"
BASELINE_FILE="$(cd -- "$(dirname "$0")" && pwd)/perf_baseline.env"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --record)
      MODE="record"
      shift
      ;;
    --check)
      MODE="check"
      shift
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --runs)
      RUNS="$2"
      shift 2
      ;;
    --test)
      TEST_REGEX="$2"
      shift 2
      ;;
    --baseline)
      BASELINE_FILE="$2"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ "$RUNS" -lt 3 ]]; then
  echo "--runs must be >= 3" >&2
  exit 2
fi

run_once() {
  local out t
  out=$(ctest --test-dir "$BUILD_DIR" -R "$TEST_REGEX" --output-on-failure)
  t=$(printf "%s\n" "$out" | sed -n 's/.*Passed[[:space:]]\+\([0-9.][0-9.]*\) sec/\1/p' | tail -n1)
  if [[ -z "$t" ]]; then
    echo "failed to parse test duration from ctest output" >&2
    exit 1
  fi
  printf "%s\n" "$t"
}

declare -a TIMES=()
for ((i=1; i<=RUNS; ++i)); do
  TIMES+=("$(run_once)")
done

mapfile -t SORTED < <(printf "%s\n" "${TIMES[@]}" | sort -n)

MEDIAN_INDEX=$((RUNS / 2))
MEDIAN_SEC="${SORTED[$MEDIAN_INDEX]}"
P95_INDEX=$(awk -v n="$RUNS" 'BEGIN { idx=int((95*n + 99)/100)-1; if (idx < 0) idx = 0; if (idx >= n) idx = n-1; print idx }')
P95_SEC="${SORTED[$P95_INDEX]}"

if [[ "$MODE" == "record" ]]; then
  {
    echo "test_regex=$TEST_REGEX"
    echo "runs=$RUNS"
    echo "median_sec=$MEDIAN_SEC"
    echo "p95_sec=$P95_SEC"
    echo "recorded_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$BASELINE_FILE"
  echo "Recorded baseline -> $BASELINE_FILE"
  echo "median_sec=$MEDIAN_SEC p95_sec=$P95_SEC runs=$RUNS"
  exit 0
fi

if [[ ! -f "$BASELINE_FILE" ]]; then
  echo "baseline file not found: $BASELINE_FILE" >&2
  echo "run with --record first" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$BASELINE_FILE"

if [[ -z "${median_sec:-}" ]]; then
  echo "baseline file is missing median_sec: $BASELINE_FILE" >&2
  exit 1
fi

THRESHOLD=$(awk -v b="$median_sec" 'BEGIN { printf "%.6f", b * 1.10 }')

echo "Perf gate: test=$TEST_REGEX runs=$RUNS"
echo "Baseline median=${median_sec}s threshold(110%)=${THRESHOLD}s current=${MEDIAN_SEC}s p95=${P95_SEC}s"

if awk -v cur="$MEDIAN_SEC" -v thr="$THRESHOLD" 'BEGIN { exit !(cur > thr) }'; then
  echo "FAIL: median regression exceeds +10%" >&2
  exit 1
fi

echo "PASS"
