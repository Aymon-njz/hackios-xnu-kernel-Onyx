#!/bin/bash
# Add the Onyx XNU payload handoff to an existing Mu-Silicium checkout.
# The resulting UEFI remains a temporary fastboot boot component: it does not
# write partitions while starting loader.efi from the appended XNUP payload.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
mu="${1:?usage: uefi/apply-onyx.sh /path/to/Mu-Silicium}"

git -C "$mu" rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
  echo "not a Mu-Silicium checkout: $mu" >&2
  exit 1
}
git -C "$mu" apply --check "$here/onyx-handoff.patch"
git -C "$mu" apply --whitespace=nowarn "$here/onyx-handoff.patch"
echo "Onyx XNU handoff applied to $mu"
