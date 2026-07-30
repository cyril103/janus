#!/usr/bin/env bash
set -euo pipefail

IMAGE=${1:-janus-reference-registry:test}
SMOKE_DIR=$(mktemp -d)
CONTAINER="janus-reference-registry-smoke-$$"

cleanup() {
  docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
  rm -r "$SMOKE_DIR"
}
trap cleanup EXIT

openssl rand -out "$SMOKE_DIR/signing.key" 32
chmod 0444 "$SMOKE_DIR/signing.key"

docker run --detach --rm --name "$CONTAINER" \
  --tmpfs \
  /var/lib/janus-registry:rw,noexec,nosuid,nodev,uid=65532,gid=65532,mode=0700 \
  --mount \
  "type=bind,src=$SMOKE_DIR/signing.key,dst=/run/secrets/registry_signing_key,readonly" \
  --env JANUS_REGISTRY_ORIGIN=https://registry.example \
  --env JANUS_REGISTRY_KEY_ID=smoke-key-v1 \
  "$IMAGE" >/dev/null

for _attempt in $(seq 1 20); do
  HEALTH=$(docker inspect --format '{{.State.Health.Status}}' "$CONTAINER")
  if [[ "$HEALTH" == "healthy" ]]; then
    docker exec "$CONTAINER" python -c \
      "import urllib.request; assert urllib.request.urlopen('http://127.0.0.1:8080/healthz').read() == b'ok\\n'"
    exit 0
  fi
  if [[ "$HEALTH" == "unhealthy" ]]; then
    docker logs "$CONTAINER"
    exit 1
  fi
  sleep 1
done

docker logs "$CONTAINER"
exit 1
