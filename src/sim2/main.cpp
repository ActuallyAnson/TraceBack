// tb_sim2: generate the topology-2 (fan-out) incidents.
//   tb_sim2 generate --out DIR [--only ID]   observable data -> DIR/incidents/<id>/, labels -> DIR/labels/, replay manifests -> DIR/replay/
//   tb_sim2 lock --manifest FILE             freeze ids and content hashes (refuses to overwrite)
//   tb_sim2 verify --manifest FILE           regenerate and compare with the frozen manifest
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "fanout.hpp"

using namespace tb;
namespace fs = std::filesystem;

constexpr uint64_t kSeed = 424242;

static void write(const fs::path& p, const std::string& body) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << body; }
static std::string arg(int argc, char** argv, const char* name, const char* def = "") {
  for (int i = 2; i + 1 < argc; ++i) if (!std::strcmp(argv[i], name)) return argv[i + 1];
  return def;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_sim2 generate|lock|verify ...\n"); return 2; }
  const std::string cmd = argv[1], out = arg(argc, argv, "--out", "runs/topo2"), only = arg(argc, argv, "--only"), manifest = arg(argc, argv, "--manifest", "eval/topology2/manifest.json");
  std::vector<std::string> lines;
  int bad = 0;
  for (sim2::Spec spec : sim2::build_catalog(kSeed)) {
    if (!only.empty() && spec.id != only) continue;
    sim::Output real, cf;
    bool ok = false;
    for (int a = 1; a <= 8 && !ok; ++a) {
      real = sim2::simulate(spec, false);
      cf = sim2::simulate(spec, true);
      ok = real.err_incident >= 0.08 && real.err_baseline < 0.03 && cf.err_incident < 0.04;  // visible, and causal: removing the cause removes it
      if (!ok) spec.seed = spec.seed * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    if (!ok) { std::fprintf(stderr, "%s: no valid realization (err=%.3f cf=%.3f base=%.3f)\n", spec.id.c_str(), real.err_incident, cf.err_incident, real.err_baseline); ++bad; continue; }
    char h[32];
    std::snprintf(h, sizeof h, "%016llx", static_cast<unsigned long long>(sim::content_hash(real)));
    lines.push_back("{\"id\":\"" + spec.id + "\",\"split\":\"test\",\"hash\":\"" + h + "\"}");
    if (cmd == "generate") {
      for (auto& [name, body] : real.files) write(fs::path(out) / "incidents" / spec.id / name, body);
      write(fs::path(out) / "labels" / (spec.id + ".json"), sim2::label_json(spec, real, cf));
      write(fs::path(out) / "replay" / (spec.id + ".json"), sim2::replay_env_json(spec));
      std::printf("%s group=%-5s err=%.2f cf_err=%.2f\n", spec.id.c_str(), spec.group.c_str(), real.err_incident, cf.err_incident);
    }
  }
  if (bad) return 1;
  std::string joined;
  for (auto& l : lines) joined += l + "\n";
  if (cmd == "lock") {
    if (fs::exists(manifest)) { std::fprintf(stderr, "%s exists; refusing to overwrite a locked manifest\n", manifest.c_str()); return 1; }
    write(manifest, joined);
    std::printf("locked %zu incidents -> %s\n", lines.size(), manifest.c_str());
  } else if (cmd == "verify") {
    std::ifstream f(manifest);
    std::stringstream ss;
    ss << f.rdbuf();
    if (ss.str() != joined) { std::fprintf(stderr, "MISMATCH: regenerated topology-2 data differs from %s\n", manifest.c_str()); return 1; }
    std::printf("OK: %zu topology-2 incidents match %s\n", lines.size(), manifest.c_str());
  }
  return 0;
}
