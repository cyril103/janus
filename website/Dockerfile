FROM python:3.13-alpine AS builder
WORKDIR /src
COPY website/requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt
COPY . .
RUN python3 website/scripts/sync_reference_docs.py \
 && mkdocs build --strict -f website/mkdocs.yml -d /site

FROM nginx:1.28-alpine
COPY website/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=builder /site /usr/share/nginx/html
EXPOSE 80
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -qO- http://127.0.0.1/healthz || exit 1
