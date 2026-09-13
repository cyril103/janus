# syntax=docker/dockerfile:1.18@sha256:dabfc0969b935b2080555ace70ee69a5261af8a8f1b4df97b9e7fbcf6722eddf
# Python 3.13 slim Bookworm (multi-platform digest; compatible with dependency wheels).
FROM python:3.13-slim-bookworm@sha256:ed86c82274b3c69b52fb5820f358f0bd7df0b603332063cb5c6e32bd220c3e6e AS builder
WORKDIR /src
COPY website/requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --require-hashes --only-binary=:all: -r /tmp/requirements.txt
COPY . .
RUN python3 website/scripts/sync_reference_docs.py \
 && mkdocs build --strict -f website/mkdocs.yml -d /site

# nginx 1.28 on Alpine (multi-platform digest).
FROM nginx:1.31-alpine@sha256:72ba65eb42c10344912a84ff42408db7d34f2feb642204570ab8fc5ffd29f1d3
COPY website/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=builder /site /usr/share/nginx/html
EXPOSE 80
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -qO- http://127.0.0.1/healthz || exit 1
