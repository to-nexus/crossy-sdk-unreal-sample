#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$PROJECT_ROOT/CrossySdkUnrealSamp.uproject"
CSV_PATH="$PROJECT_ROOT/Localization/DT_DappStrings.csv"
PY_SCRIPT="$PROJECT_ROOT/scripts/import_dapp_strings.py"

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.7}"
UNREAL_EDITOR="${UNREAL_EDITOR:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor}"

[[ -f "$PROJECT" ]] || { echo "[err] project not found: $PROJECT" >&2; exit 1; }
[[ -f "$CSV_PATH" ]] || { echo "[err] CSV not found: $CSV_PATH" >&2; exit 1; }
[[ -f "$PY_SCRIPT" ]] || { echo "[err] Python script not found: $PY_SCRIPT" >&2; exit 1; }
[[ -x "$UNREAL_EDITOR" ]] || { echo "[err] UnrealEditor not executable: $UNREAL_EDITOR" >&2; exit 1; }

"$UNREAL_EDITOR" "$PROJECT" \
  -ExecutePythonScript="$PY_SCRIPT" \
  -unattended \
  -nop4 \
  -NullRHI \
  -NoSplash

echo "[done] Reimported Localization/DT_DappStrings.csv into /Game/Localization/DT_DappStrings"
