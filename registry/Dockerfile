# syntax=docker/dockerfile:1.18@sha256:dabfc0969b935b2080555ace70ee69a5261af8a8f1b4df97b9e7fbcf6722eddf
FROM python:3.13.5-slim-bookworm@sha256:4c2cf9917bd1cbacc5e9b07320025bdb7cdf2df7b0ceaccb55e9dd7e30987419

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    JANUS_REGISTRY_DATA=/var/lib/janus-registry \
    JANUS_REGISTRY_LISTEN=0.0.0.0 \
    JANUS_REGISTRY_PORT=8080

WORKDIR /app
COPY --chown=65532:65532 reference_registry /app/reference_registry
RUN install -d -o 65532 -g 65532 -m 0700 /var/lib/janus-registry

USER 65532:65532
EXPOSE 8080
VOLUME ["/var/lib/janus-registry"]
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD ["python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/healthz', timeout=2).read()"]

ENTRYPOINT ["python", "-m", "reference_registry"]
CMD ["--signing-key-file", "/run/secrets/registry_signing_key"]
