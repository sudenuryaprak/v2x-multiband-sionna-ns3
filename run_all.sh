#!/usr/bin/env bash
set -u
set -o pipefail

# Usage:
#   ./run_all.sh [NS3_DIR]
#
# Example:
#   ./run_all.sh ~/ns-3-dev-v51
#
# Runs the three validated reference scenarios and stores outputs
# under this repository's results/ directory.

NS3_DIR="${1:-$HOME/ns-3-dev-v51}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$REPO_DIR/results"
SCRATCH_DIR="$NS3_DIR/scratch"

echo "== V2X reference simulations =="
echo "Repo     : $REPO_DIR"
echo "ns-3 dir : $NS3_DIR"
echo "Results  : $RESULTS_DIR"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "ERROR: '$NS3_DIR/ns3' was not found or is not executable."
    exit 1
fi

required_scenarios=(
    "arac-munih-baseline-fr1-duzeltilmis.cc"
    "arac-munih-static-loglu-fix.cc"
    "arac-munih-dinamik-sensor-histerezis.cc"
)

for file in "${required_scenarios[@]}"; do
    if [[ ! -f "$SCRATCH_DIR/$file" ]]; then
        echo "ERROR: missing $SCRATCH_DIR/$file"
        echo "Run setup.sh first:"
        echo "  ./setup.sh \"$NS3_DIR\""
        exit 1
    fi
done

mkdir -p "$RESULTS_DIR"

echo
echo "Building ns-3 ..."
cd "$NS3_DIR"
if ! ./ns3 build -j 2; then
    echo "ERROR: build failed."
    exit 1
fi

run_case() {
    local label="$1"
    local scenario="$2"
    local outfile="$3"

    echo
    echo "============================================================"
    echo "Running: $label"
    echo "Scenario: $scenario"
    echo "Output  : $outfile"
    echo "============================================================"

    ./ns3 run "scratch/$scenario --sionna=1 --log=4" 2>&1 | tee "$outfile"
    local status=${PIPESTATUS[0]}

    if [[ $status -eq 0 ]]; then
        echo
        echo "[OK] $label completed successfully."
    else
        echo
        echo "[FAILED] $label exited with status $status."
    fi

    return $status
}

baseline_status=0
static_status=0
dynamic_status=0

run_case \
    "Single-band FR1 baseline" \
    "arac-munih-baseline-fr1-duzeltilmis" \
    "$RESULTS_DIR/sonuc-baseline-fr1.txt" || baseline_status=$?

run_case \
    "Static dual-band" \
    "arac-munih-static-loglu-fix" \
    "$RESULTS_DIR/sonuc-statik.txt" || static_status=$?

run_case \
    "Dynamic dual-band + hysteresis" \
    "arac-munih-dinamik-sensor-histerezis" \
    "$RESULTS_DIR/sonuc-dinamik.txt" || dynamic_status=$?

echo
echo "============================================================"
echo "SUMMARY"
echo "============================================================"
printf "%-32s : %s\n" "Single-band FR1 baseline" "$([[ $baseline_status -eq 0 ]] && echo OK || echo FAILED)"
printf "%-32s : %s\n" "Static dual-band" "$([[ $static_status -eq 0 ]] && echo OK || echo FAILED)"
printf "%-32s : %s\n" "Dynamic dual-band + hysteresis" "$([[ $dynamic_status -eq 0 ]] && echo OK || echo FAILED)"

echo
echo "Result files:"
echo "  $RESULTS_DIR/sonuc-baseline-fr1.txt"
echo "  $RESULTS_DIR/sonuc-statik.txt"
echo "  $RESULTS_DIR/sonuc-dinamik.txt"

if [[ $baseline_status -ne 0 || $static_status -ne 0 || $dynamic_status -ne 0 ]]; then
    echo
    echo "At least one scenario failed. Check the corresponding result file."
    exit 1
fi

echo
echo "All reference simulations completed successfully."
