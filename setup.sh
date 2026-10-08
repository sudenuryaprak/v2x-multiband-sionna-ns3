#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./setup.sh [NS3_DIR]
#
# Example:
#   ./setup.sh ~/ns-3-dev-v51
#
# If NS3_DIR is omitted, ~/ns-3-dev-v51 is used.

NS3_DIR="${1:-$HOME/ns-3-dev-v51}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRATCH_DIR="$NS3_DIR/scratch"

echo "== V2X project setup =="
echo "Repo     : $REPO_DIR"
echo "ns-3 dir : $NS3_DIR"
echo "Scratch  : $SCRATCH_DIR"

if [[ ! -d "$NS3_DIR" ]]; then
    echo "ERROR: ns-3 directory not found: $NS3_DIR"
    exit 1
fi

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "ERROR: '$NS3_DIR/ns3' was not found or is not executable."
    echo "Make sure this is an ns-3 3.48 + 5G-LENA v5.1 working directory."
    exit 1
fi

if [[ ! -d "$SCRATCH_DIR" ]]; then
    echo "ERROR: scratch directory not found: $SCRATCH_DIR"
    exit 1
fi

required_files=(
    "ns3/arac-munih-baseline-fr1-duzeltilmis.cc"
    "ns3/arac-munih-static-loglu-fix.cc"
    "ns3/arac-munih-dinamik-sensor-histerezis.cc"
    "ns3/sionna-iz.h"
    "ns3/iz_munih.csv"
    "ns3/iz_munih_fr2.csv"
)

for rel in "${required_files[@]}"; do
    if [[ ! -f "$REPO_DIR/$rel" ]]; then
        echo "ERROR: required project file is missing: $REPO_DIR/$rel"
        exit 1
    fi
done

echo
echo "Copying project files to ns-3 scratch/ ..."

cp "$REPO_DIR/ns3/arac-munih-baseline-fr1-duzeltilmis.cc" "$SCRATCH_DIR/"
cp "$REPO_DIR/ns3/arac-munih-static-loglu-fix.cc" "$SCRATCH_DIR/"
cp "$REPO_DIR/ns3/arac-munih-dinamik-sensor-histerezis.cc" "$SCRATCH_DIR/"
cp "$REPO_DIR/ns3/sionna-iz.h" "$SCRATCH_DIR/"
cp "$REPO_DIR/ns3/iz_munih.csv" "$SCRATCH_DIR/"
cp "$REPO_DIR/ns3/iz_munih_fr2.csv" "$SCRATCH_DIR/"

echo
echo "Copied files:"
ls -lh \
    "$SCRATCH_DIR/arac-munih-baseline-fr1-duzeltilmis.cc" \
    "$SCRATCH_DIR/arac-munih-static-loglu-fix.cc" \
    "$SCRATCH_DIR/arac-munih-dinamik-sensor-histerezis.cc" \
    "$SCRATCH_DIR/sionna-iz.h" \
    "$SCRATCH_DIR/iz_munih.csv" \
    "$SCRATCH_DIR/iz_munih_fr2.csv"

echo
echo "Building ns-3 ..."
cd "$NS3_DIR"
./ns3 build -j 2

echo
echo "Setup completed successfully."
echo
echo "Next step:"
echo "  cd \"$REPO_DIR\""
echo "  ./run_all.sh \"$NS3_DIR\""
