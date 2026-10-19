.PHONY: v2 v2-check v1-check live build test data verify eval-dev site serve results results-check demo topology2 topology2-check
build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
test: build
	ctest --test-dir build --output-on-failure
# Regenerate all labeled incidents (deterministic). Observable data -> runs/incidents, labels -> runs/labels.
data: build
	./build/tb_sim generate --out runs
# Fail if regenerated data differs from the frozen split/hashes in eval/split.json.
verify: build
	./build/tb_sim verify --manifest eval/split.json
# Score the deterministic analyzer on the dev split (the only split used for tuning).
eval-dev: data
	python3 eval/score.py --split dev
# Rebuild the showcase site data (docs/data) from saved runs and cached model answers; no new API calls.
site: build
	python3 eval/build_site.py
# Live mode: same UI, but "Trigger an outage" re-runs the simulator, analyzer and verifier for real (model answers recorded unless you opt in).
live: build
	python3 tools/live_server.py 8000
# Preview the static showcase site at http://localhost:8000
serve:
	python3 -m http.server 8000 -d docs

# Regenerate every reported number from saved runs (offline: model answers come from eval/llm_cache) and refresh README.
results: build
	python3 eval/build_site.py
	python3 eval/make_results.py
# Fail if the README table differs from what the saved runs produce.
results-check: build
	python3 eval/build_site.py >/dev/null
	python3 eval/make_results.py --check

# One-command demo: build, generate one incident, run the full investigation, write the report.
# Uses the recorded model answer in eval/llm_cache, so it needs no API key. To call the model live
# for an incident that is not cached, set NEBIUS_API_KEY.
DEMO_INC ?= inc-026
demo: build
	@./build/tb_sim generate --out runs --only $(DEMO_INC) | tail -1
	@./build/tb_investigate runs/incidents/$(DEMO_INC) --replay runs/replay/$(DEMO_INC).json --cache eval/llm_cache --out runs/demo
	@echo
	@echo "Open runs/demo/report.html (or report.md). Same pipeline powers the web UI: make serve"

# Generalization check on the second topology (fan-out with a broker). Offline: model answers come from eval/llm_cache.
topology2: build
	./build/tb_sim2 generate --out runs/topo2 | tail -1
	./build/tb_sim2 verify --manifest eval/topology2/manifest.json
	python3 eval/topology2/report.py
topology2-check: build
	./build/tb_sim2 generate --out runs/topo2 >/dev/null
	python3 eval/topology2/report.py --check

# v2 evaluation on the fresh sets (eval/v2/PLAN.md). Offline: model answers come from eval/llm_cache.
v2: build
	python3 eval/v2/report.py
v2-check: build
	python3 eval/v2/report.py --check
# v1 results, locked splits, scoring protocol and frozen analyzer/prompts are unchanged since the v1 freeze.
v1-check:
	sh eval/v2/check_v1.sh
explain: build
	python3 eval/explain/run.py --set fresh
explain-check: build
	python3 eval/explain/run.py --set fresh --check
# Real telemetry (OpenTelemetry Demo): the converted incidents are committed as tarballs; raw OTLP files are not.
real-data:
	mkdir -p eval/real/v0 eval/real/v1
	[ -d eval/real/v0/incidents ] || tar -xzf eval/real/incidents_v0.tgz -C eval/real/v0
	[ -d eval/real/v1/incidents ] || tar -xzf eval/real/incidents_v1.tgz -C eval/real/v1
real: build real-data
	python3 eval/real/evaluate.py
real-check: build real-data
	python3 eval/real/evaluate.py --check
real2-data:
	mkdir -p eval/real2/data
	[ -d eval/real2/data/incidents ] || tar -xzf eval/real2/incidents.tgz -C eval/real2/data
real2: build real2-data
	python3 eval/real2/evaluate.py
real2-check: build real2-data real-data
	python3 eval/real2/evaluate.py --check
