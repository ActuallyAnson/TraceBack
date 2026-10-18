.PHONY: build test data verify eval-dev
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
