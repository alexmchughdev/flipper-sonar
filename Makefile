# Flipper Claudeogotchi — one-stop setup & run.
# Run `make` (or `make help`) to see everything.

RELAY_PORT ?= 8787
RELAY      ?= http://127.0.0.1:$(RELAY_PORT)
WS_RELAY   := $(subst http,ws,$(RELAY))
ID         ?=

# macOS python.org builds ship without root certs, which breaks ufbt's SDK
# download. Point it at certifi's bundle automatically when available.
export SSL_CERT_FILE := $(shell python3 -c "import certifi;print(certifi.where())" 2>/dev/null)
UFBT := python3 -m ufbt

.DEFAULT_GOAL := help

.PHONY: help
help: ## Show this help
	@echo "Flipper Claudeogotchi"
	@echo
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
	  | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-12s\033[0m %s\n",$$1,$$2}'
	@echo
	@echo "Typical no-board run:"
	@echo "  make setup           # install deps (one time)"
	@echo "  make flash           # build + upload Clawd to a connected Flipper"
	@echo "  make relay &         # start the local relay"
	@echo "  make pair   ID=XXXXXX   # wire up this machine's Claude Code"
	@echo "  make bridge ID=XXXXXX   # stream to the Flipper over USB (Link=USB)"

.PHONY: setup
setup: ## Install all dependencies (relay deps + ufbt)
	cd relay && npm install
	@python3 -m pip show ufbt >/dev/null 2>&1 || python3 -m pip install --user ufbt
	@echo "setup complete."

.PHONY: test
test: ## Run every test suite (proto + relay + installer + e2e)
	cd proto && node --test --experimental-strip-types test/*.test.ts
	cd relay && node --test --experimental-strip-types
	cd installer && node --test test/*.test.js
	node --test --experimental-strip-types e2e/*.test.mjs

.PHONY: proto-check
proto-check: ## Verify the C and TS UART encoders are byte-identical
	cc -std=c11 -Iproto -o /tmp/_gf proto/test/gen_frames.c && /tmp/_gf > /tmp/_c.txt
	node --experimental-strip-types proto/test/gen_frames.ts > /tmp/_ts.txt
	diff /tmp/_c.txt /tmp/_ts.txt && echo "C and TS frames are byte-identical"

.PHONY: relay
relay: ## Run the relay in self-host mode (plaintext, trusted LAN)
	cd relay && node src/index.ts --self-host --port $(RELAY_PORT)

.PHONY: fap
fap: ## Build the Flipper app (.fap) with ufbt
	cd fap && $(UFBT)

.PHONY: flash
flash: ## Build + upload + launch Clawd on a connected Flipper
	cd fap && $(UFBT) launch

.PHONY: pair
pair: ## Wire up Claude Code here: make pair ID=AB12CD [RELAY=http://host:8787]
	@test -n "$(ID)" || { echo "usage: make pair ID=<pairing-code> [RELAY=http://host:8787]"; exit 1; }
	node installer/index.js --pair $(ID) --relay $(RELAY)

.PHONY: unpair
unpair: ## Remove everything the installer added from Claude Code
	node installer/index.js --uninstall

.PHONY: bridge
bridge: ## No-board USB stream: make bridge ID=AB12CD [RELAY=ws://host:8787]
	@test -n "$(ID)" || { echo "usage: make bridge ID=<pairing-code> [RELAY=ws://host:8787]"; exit 1; }
	node installer/runtime/claudeogotchi-usb-bridge.mjs --claudeogotchi $(ID) --relay $(WS_RELAY)

.PHONY: sprite
sprite: ## Re-render the Clawd sprite + full-screen mockup (docs/img)
	python3 fap/tools_clawd_preview.py
	python3 fap/tools_screen_preview.py && cp /tmp/screen.png docs/img/claudeogotchi-screen.png

.PHONY: clean
clean: ## Remove build artifacts and deps
	rm -rf relay/node_modules fap/dist fap/.ufbt
