#!/usr/bin/env bash

set -Eeuo pipefail

BACKUP_ROOT="${BACKUP_ROOT:-$HOME/dev-backups}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARCHIVE="${BACKUP_ROOT}/dev-environment-${TIMESTAMP}.tar.gz"
STAGING="$(mktemp -d)"

cleanup() {
  rm -rf "$STAGING"
}
trap cleanup EXIT

mkdir -p "$BACKUP_ROOT"

if [[ "$(id -u)" -ne 0 ]] && ! command -v sudo >/dev/null 2>&1; then
  echo "Ошибка: для чтения /etc нужен root или sudo." >&2
  exit 1
fi

mkdir -p "$STAGING/metadata" "$STAGING/vscode"

date --iso-8601=seconds > "$STAGING/metadata/created-at.txt"
uname -a > "$STAGING/metadata/uname.txt"
lsb_release -a > "$STAGING/metadata/lsb-release.txt" 2>&1 || true
dpkg-query -W -f='${binary:Package}\t${Version}\n' > "$STAGING/metadata/packages.txt" 2>/dev/null || true

if command -v code >/dev/null 2>&1; then
  code --list-extensions --show-versions > "$STAGING/vscode/extensions.txt" 2>&1 || true
else
  echo "Команда code не найдена" > "$STAGING/vscode/extensions.txt"
fi

for path in \
  "$HOME/.config/Code/User/settings.json" \
  "$HOME/.config/Code/User/keybindings.json" \
  "$HOME/.config/Code/User/snippets" \
  "$HOME/.config/Code/User/profiles" \
  "$HOME/.vscode"; do
  if [[ -e "$path" ]]; then
    mkdir -p "$STAGING/vscode/$(dirname "${path#"$HOME/"}")"
    cp -a "$path" "$STAGING/vscode/${path#"$HOME/"}"
  fi
done

cat > "$STAGING/metadata/paths.txt" <<EOF
/etc/
/home/work/Fipik
/home/work/ESP32-ASProject
/home/work/RXTX433
EOF

mapfile -t EXISTING_PATHS < <(
  for path in /etc /home/work/Fipik /home/work/ESP32-ASProject /home/work/RXTX433; do
    [[ -e "$path" ]] && printf '%s\n' "$path"
  done
)

if [[ "${#EXISTING_PATHS[@]}" -eq 0 ]]; then
  echo "Ошибка: не найден ни один каталог для архивации." >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  tar -czf "$ARCHIVE" -C / "${EXISTING_PATHS[@]#/}" -C "$STAGING" .
else
  sudo tar -czf "$ARCHIVE" -C / "${EXISTING_PATHS[@]#/}" -C "$STAGING" .
fi

sha256sum "$ARCHIVE" > "${ARCHIVE}.sha256"

echo "Архив создан: $ARCHIVE"
echo "Контрольная сумма: ${ARCHIVE}.sha256"
