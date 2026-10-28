#!/bin/sh
# Passive host diagnostics only; never opens a device, loads a kext or queries amp registers.
set -eu
out=${1:?provide an output directory under the research workspace}
case "$out" in /Users/zhangyirui/Documents/Codex/*) ;; *) printf '%s\n' 'Output directory outside research workspace' >&2; exit 2;; esac
mkdir -p "$out"
date -u '+%Y-%m-%dT%H:%M:%SZ' > "$out/time.txt"
sw_vers > "$out/sw-vers.txt"
uname -a > "$out/uname.txt"
nvram boot-args > "$out/boot-args.txt" 2>&1 || true
kmutil showloaded > "$out/kexts.txt" 2>&1 || true
system_profiler SPAudioDataType > "$out/audio.txt" 2>&1 || true
ioreg -r -l -w0 -c VoodooHDADevice > "$out/voodoohda-ioreg.txt" 2>&1 || true
ioreg -r -l -w0 -c CirrusAudioFixup > "$out/cirrus-ioreg.txt" 2>&1 || true
