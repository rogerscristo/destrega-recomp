#!/bin/sh
# Export REA (Ghidra) procedure inventories for every overlay ELF.
cd "$(dirname "$0")/.."
# Ghidra/Java locations for the REA provider (override via environment).
export GHIDRA_INSTALL_DIR=${GHIDRA_INSTALL_DIR:-/opt/homebrew/opt/ghidra/libexec}
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/Cellar/openjdk@21/21.0.12.1/libexec/openjdk.jdk/Contents/Home}
for elf in build/elf/*.elf; do
  n=$(basename "$elf" .elf)
  [ "$n" = "SLUS_007.32" ] && continue
  out=analysis/overlays/$n.procs.json
  [ -s "$out" ] && continue
  npx -y rea-agents@6.7.0 search "$PWD/$elf" --kind procedures --mode regex --pattern '.*' \
      --provider ghidra --format json > "$out.tmp" 2> "analysis/overlays/$n.err" && mv "$out.tmp" "$out"
  echo "$n $(wc -c < "$out" 2>/dev/null)"
done
