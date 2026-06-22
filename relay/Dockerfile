# Flipper Sonar relay — self-host image.
# Telemetry-only reverse WS tunnel. No database, in-memory last-state per topic.
FROM node:22-alpine

WORKDIR /app

# Install production deps only (just `ws`).
COPY package.json package-lock.json* ./
RUN npm install --omit=dev --no-audit --no-fund

COPY src ./src

ENV PORT=8787
ENV HOST=0.0.0.0
EXPOSE 8787

# Run behind a TLS-terminating proxy (recommended) which sets
# x-forwarded-proto: https, OR pass --self-host to allow plaintext ingest for
# local/trusted-network use, OR mount certs and set TLS_CERT/TLS_KEY.
HEALTHCHECK --interval=30s --timeout=3s \
  CMD wget -qO- http://127.0.0.1:8787/healthz || exit 1

ENTRYPOINT ["node", "src/index.ts"]
