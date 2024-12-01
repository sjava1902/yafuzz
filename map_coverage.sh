#!/bin/bash

VMLINUX_PATH="/home/slava/linux/vmlinux"
COVERAGE_FILE="output.txt"
OUTPUT_FILE="mapped_coverage.txt"

while read -r address; do
    addr2line -e "$VMLINUX_PATH" -a -f "$address" >> "$OUTPUT_FILE"
done < "$COVERAGE_FILE"

echo "Покрытие отображено в $OUTPUT_FILE"
