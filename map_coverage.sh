#!/bin/bash

VMLINUX_PATH="/home/slava/linux/vmlinux"
COVERAGE_FILE="output.txt"
OUTPUT_FILE="mapped_coverage.txt"

llvm-addr2line -e "$VMLINUX_PATH" -a -f -C < output.txt > "$OUTPUT_FILE"

echo "Покрытие отображено в $OUTPUT_FILE"
