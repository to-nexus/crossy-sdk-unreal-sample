# ============================================================================
#  CrossySdkUnrealSamp — convenience Makefile
#
#  Group 1: SDK plugin management (Phase 1)
#  Group 2: Build / device automation (Phase 4; placeholders for now)
#
#  Environment:
#    GITHUB_TOKEN  Required for sdk-install / sdk-update. Fine-grained PAT with
#                  contents:read on the registry repo defined in
#                  crossx-plugins.json (default: to-nexus/crossy-sdk-unreal).
#
#                  Can be provided via either:
#                    (a) shell export:  export GITHUB_TOKEN=...
#                    (b) local .env:    echo 'GITHUB_TOKEN=...' > .env
#                        (.env is .gitignore'd; this Makefile auto-loads it.)
# ============================================================================

SHELL := /usr/bin/env bash
.ONESHELL:
.DEFAULT_GOAL := help

# Auto-load `.env` (and `.env.local` override) so developers don't have to
# remember to `export GITHUB_TOKEN` in every new terminal. `export` forwards
# loaded variables into recipe subshells.
ifneq (,$(wildcard .env))
    include .env
    export
endif
ifneq (,$(wildcard .env.local))
    include .env.local
    export
endif

MANIFEST := crossx-plugins.json
LOCK     := crossx-plugins.lock.json

# ----------- help -----------------------------------------------------------

.PHONY: help
help: ## Show this help
	@awk 'BEGIN {FS=":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n\nTargets:\n"} \
		/^[a-zA-Z_-]+:.*?##/ {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)

# ----------- Group 1: SDK plugin management ---------------------------------

.PHONY: sdk-install sdk-update sdk-verify sdk-clean

sdk-install: ## Install / reconcile plugins per crossx-plugins.json  (needs $$GITHUB_TOKEN)
	@./scripts/install-plugins.sh

sdk-update: ## Bump a single plugin. Ex: make sdk-update name=CROSSxSdkUnrealPlugin version=0.3.0
	@if [[ -z "$(name)" || -z "$(version)" ]]; then \
		echo "usage: make sdk-update name=<PluginName> version=<semver>"; exit 2; \
	fi
	@tmp=$$(mktemp); \
		jq --arg n "$(name)" --arg v "$(version)" '.plugins[$$n] = $$v' $(MANIFEST) > $$tmp && mv $$tmp $(MANIFEST); \
		echo "manifest: $(name) -> $(version)"
	@./scripts/install-plugins.sh

sdk-verify: ## Check installed .uplugin VersionName matches manifest/lock
	@./scripts/install-plugins.sh --verify

sdk-clean: ## Remove ./Plugins/CROSSx* (next sdk-install will re-fetch)
	@rm -rf Plugins/CROSSxSdkUnrealPlugin Plugins/CROSSxRampSdkUnrealPlugin
	@echo "cleaned Plugins/CROSSx*"

# ----------- Group 2: build / device automation (Phase 4 placeholders) -----
#  These targets are intentionally left as TODOs; Phase 4 of the work plan
#  wires them up (package, ios-install, android-install, run, etc.).

.PHONY: ios android win64 install run
ios:     ; @echo "TODO(Phase 4): iOS packaging"
android: ; @echo "TODO(Phase 4): Android packaging"
win64:   ; @echo "TODO(Phase 4): Win64 packaging"
install: ; @echo "TODO(Phase 4): deploy to connected device"
run:     ; @echo "TODO(Phase 4): run on connected device"
