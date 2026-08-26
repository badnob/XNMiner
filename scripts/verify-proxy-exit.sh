#!/usr/bin/env bash
# Run on a cheap VPS (not on Vast). Listens SOCKS5 with auth; one public IP = one
# unique /verify egress. Point a Vast box at it with:
#   export VERIFY_PROXY=socks5h://USER:PASS@THIS.VPS.IP:1080
set -euo pipefail

PORT="${PORT:-1080}"
USER_NAME="${SOCKS_USER:-xnverify}"
PASS="${SOCKS_PASS:-}"
if [[ -z "${PASS}" ]]; then
  PASS="$(head -c 12 /dev/urandom | base64 | tr -d '/+=' | head -c 16)"
fi

export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null 2>&1; then
  apt-get update -y
  apt-get install -y --no-install-recommends microsocks ca-certificates
elif command -v dnf >/dev/null 2>&1; then
  dnf install -y microsocks
else
  echo "ERROR: install microsocks by hand on this distro." >&2
  exit 1
fi

install -d -m 0755 /etc/xnminer
umask 077
printf '%s\n' "${PASS}" > /etc/xnminer/socks.pass
chmod 600 /etc/xnminer/socks.pass

cat >/etc/systemd/system/xnminer-socks.service <<EOF
[Unit]
Description=SOCKS5 exit for xnminer POST /verify
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/microsocks -i 0.0.0.0 -p ${PORT} -u ${USER_NAME} -P ${PASS}
Restart=always
RestartSec=2
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now xnminer-socks.service

if command -v ufw >/dev/null 2>&1; then
  ufw allow "${PORT}/tcp" || true
fi
if command -v firewall-cmd >/dev/null 2>&1; then
  firewall-cmd --add-port="${PORT}/tcp" --permanent || true
  firewall-cmd --reload || true
fi

IP="$(curl -4 -fsS --max-time 8 https://ifconfig.me || curl -4 -fsS --max-time 8 https://api.ipify.org || hostname -I | awk '{print $1}')"
echo
echo "SOCKS5 exit is up."
echo "  VERIFY_PROXY=socks5h://${USER_NAME}:${PASS}@${IP}:${PORT}"
echo
echo "Test from a Vast box:"
echo "  curl -4 -fsS --max-time 15 -x 'socks5h://${USER_NAME}:${PASS}@${IP}:${PORT}' https://ifconfig.me"
echo "  # must print ${IP}  — if it prints the Vast IP, the proxy is not in use"
