#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <stock-bootloader.bin> <validator-patch.bin> <patched-bootloader.bin>" >&2
    exit 2
fi

stock_path=$1
validator_path=$2
output_path=$3
expected_sha256=816c47f38e3317d4ff691b85fab26f499e380bd1a24ac2c389743893620b45d5

actual_sha256=$(sha256sum "$stock_path" | cut -d' ' -f1)
if [[ "$actual_sha256" != "$expected_sha256" ]]; then
    echo "Refusing to patch an unknown bootloader: SHA-256 is $actual_sha256" >&2
    exit 1
fi

if (( $(stat -c %s "$validator_path") > 0x48 )); then
    echo "Compact-header validator exceeds the stock 0x48-byte function slot" >&2
    exit 1
fi

check_word()
{
    local offset=$1
    local expected=$2
    local actual
    actual=$(od -An -tx1 -N4 -j "$offset" "$stock_path" | tr -d ' \n')
    if [[ "$actual" != "$expected" ]]; then
        printf 'Unexpected instruction at 0x%X: expected %s, found %s\n' \
            "$offset" "$expected" "$actual" >&2
        exit 1
    fi
}

write_word()
{
    local offset=$1
    local value=$2
    printf '%s' "$value" | xxd -r -p | \
        dd of="$output_path" bs=1 seek="$offset" conv=notrunc status=none
}

check_word $((0x00004610)) 3bff0010
check_word $((0x00012AF0)) 3c60002f
check_word $((0x00012B44)) 3d800030
check_word $((0x00012B48)) 818cffe8
check_word $((0x00012B74)) 3d800030
check_word $((0x00012B78)) 818cffec
check_word $((0x00012BA0)) 3d800030
check_word $((0x00012BA4)) 818cfff0
check_word $((0x00012BCC)) 3d800030
check_word $((0x00012BD0)) 818cfff4

cp "$stock_path" "$output_path"

# The patched application begins at 0x80018 instead of 0x80010.
write_word $((0x00004610)) 3bff0018

# Replace the fixed 0x2FFFF8 marker check with the header-length validator.
dd if="$validator_path" of="$output_path" bs=1 seek=$((0x00012AF0)) \
    conv=notrunc status=none

# Redirect the four callback loads to the aligned table at 0x80004.
write_word $((0x00012B44)) 3d800008
write_word $((0x00012B48)) 818c0004
write_word $((0x00012B74)) 3d800008
write_word $((0x00012B78)) 818c0008
write_word $((0x00012BA0)) 3d800008
write_word $((0x00012BA4)) 818c000c
write_word $((0x00012BCC)) 3d800008
write_word $((0x00012BD0)) 818c0010

printf 'Patched stock bootloader for compact application header: %s\n' "$output_path"
