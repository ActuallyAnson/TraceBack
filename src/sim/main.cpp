// tb_sim: generate labeled incidents.
//   tb_sim generate --out DIR [--only ID]      write observable data + labels
//   tb_sim lock     --manifest FILE            freeze ids/splits/hashes (refuses to overwrite)
//   tb_sim verify   --manifest FILE            regenerate and compare hashes to the frozen manifest
// Observable data goes to DIR/incidents/<id>/, ground truth to DIR/labels/<id>.json.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "sim.hpp"

using namespace tb::sim;
namespace fs = std::filesystem;

constexpr uint64_t kMasterSeed = 20261019;
constexpr uint64_t kVariantSeed = 7770001;
constexpr uint64_t kV2DevSeed = 31415001, kV2FreshSeed = 27182002;  // v2 sets (eval/v2/PLAN.md)  // unseen variants have their own RNG stream

namespace {

void write(const fs::path& p, const std::string& body) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << body;
}

struct Built {
  Spec spec;
  Output real, cf;
  bool ok = false;
};

// An incident is only kept if it is real: the symptom must be visible, and removing the
// true cause must make it go away. Seeds are re-drawn otherwise (attempts are recorded).
Built build(Spec spec, int* attempts) {
  Built b;
  for (int a = 1; a <= 8; ++a) {
    *attempts = a;
    b.real = simulate(spec, false);
    b.cf = simulate(spec, true);
    const bool visible = b.real.err_incident >= 0.08 && b.real.err_baseline < 0.03;
    const bool causal = b.cf.err_incident < 0.04;
    if (visible && causal) {
      b.spec = spec;
      b.ok = true;
      return b;
    }
    spec.seed = spec.seed * 6364136223846793005ULL + 1442695040888963407ULL;
  }
  b.spec = spec;
  return b;
}

std::string arg(int argc, char** argv, const char* name, const char* def = "") {
  for (int i = 2; i + 1 < argc; ++i)
    if (!std::strcmp(argv[i], name)) return argv[i + 1];
  return def;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_sim generate|lock|verify ...\n"); return 2; }
  const std::string cmd = argv[1];
  const std::string out = arg(argc, argv, "--out", "runs");
  const std::string only = arg(argc, argv, "--only");
  const std::string manifest = arg(argc, argv, "--manifest", "eval/split.json");

  std::vector<std::string> lines;
  int bad = 0;
  std::vector<Spec> catalog;
  const std::string set = arg(argc, argv, "--set", "v1");  // v1 (locked) | v2dev (development only) | v2fresh (evaluation, locked before use)
  if (set == "v2dev") catalog = build_v2_set(kV2DevSeed, false, "d1");
  else if (set == "v2fresh") catalog = build_v2_set(kV2FreshSeed, true, "f1");
  else {
    catalog = build_catalog(kMasterSeed);
    for (auto& v : build_variants(kVariantSeed, catalog.size())) catalog.push_back(v);
  }
  for (const Spec& s0 : catalog) {
    if (!only.empty() && s0.id != only) continue;
    int attempts = 0;
    Built b = build(s0, &attempts);
    if (!b.ok) {
      std::fprintf(stderr, "%s [%s/%s i1=%d p1=%.2f p2=%.2f]: no valid realization after %d attempts (err=%.3f cf=%.3f base=%.3f)\n", s0.id.c_str(), kind_name(s0.faults[0].kind), kSvcName[s0.faults[0].svc], s0.faults[0].i1, s0.faults[0].p1, s0.faults[0].p2, attempts, b.real.err_incident, b.cf.err_incident, b.real.err_baseline);
      ++bad;
      continue;
    }
    char h[32];
    std::snprintf(h, sizeof h, "%016llx", static_cast<unsigned long long>(content_hash(b.real)));
    lines.push_back("{\"id\":\"" + b.spec.id + "\",\"split\":\"" + b.spec.split + "\",\"hash\":\"" + h + "\"}");
    if (cmd == "generate") {
      for (auto& [name, body] : b.real.files) write(fs::path(out) / "incidents" / b.spec.id / name, body);
      write(fs::path(out) / "labels" / (b.spec.id + ".json"), label_json(b.spec, b.real, b.cf));
      write(fs::path(out) / "replay" / (b.spec.id + ".json"), replay_env_json(b.spec));  // verifier input: no causal marker
      std::printf("%s group=%-6s split=%-4s attempts=%d err=%.2f cf_err=%.2f\n", b.spec.id.c_str(), b.spec.group.c_str(), b.spec.split.c_str(), attempts, b.real.err_incident, b.cf.err_incident);
    }
  }
  if (bad) return 1;

  std::string joined;
  for (auto& l : lines) joined += l + "\n";
  if (cmd == "lock" && std::string(arg(argc, argv, "--append", "no")) != "no") {
    // Append mode: every existing line must be unchanged; only new incidents may be added.
    std::ifstream f(manifest);
    std::string line;
    size_t n = 0;
    while (std::getline(f, line)) {
      if (n >= lines.size() || lines[n] != line) { std::fprintf(stderr, "refusing: existing locked line %zu changed\n", n + 1); return 1; }
      ++n;
    }
    write(manifest, joined);
    std::printf("appended %zu new incidents (%zu locked before) -> %s\n", lines.size() - n, n, manifest.c_str());
  } else if (cmd == "lock") {
    if (fs::exists(manifest)) { std::fprintf(stderr, "%s exists; refusing to overwrite a locked split\n", manifest.c_str()); return 1; }
    write(manifest, joined);
    std::printf("locked %zu incidents -> %s\n", lines.size(), manifest.c_str());
  } else if (cmd == "verify") {
    std::ifstream f(manifest);
    std::stringstream ss;
    ss << f.rdbuf();
    if (ss.str() != joined) { std::fprintf(stderr, "MISMATCH: regenerated data differs from %s\n", manifest.c_str()); return 1; }
    std::printf("OK: %zu incidents match %s\n", lines.size(), manifest.c_str());
  }
  return 0;
}
