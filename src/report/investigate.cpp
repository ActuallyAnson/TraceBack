// tb_investigate <incident_dir> [--replay manifest] [--out dir] [--cache dir] [--model M]
// Full pipeline: deterministic analysis -> LLM reasoning with citation checks -> counterfactual verification
// -> readable report (CLI summary, report.md, report.html, report.json).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "tb/reasoner.hpp"
#include "tb/verify.hpp"

using namespace tb;
namespace fs = std::filesystem;

namespace {

std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else o += c;
  }
  return o;
}
std::string pct(double x) { char b[16]; std::snprintf(b, sizeof b, "%.1f%%", x * 100); return b; }
std::string rel(int64_t t, int64_t t0) { char b[32]; std::snprintf(b, sizeof b, "%+.1fs", (t - t0) / 1e6); return b; }

struct Report {
  std::string id, model, category, service, summary, llm_conf, final_conf, vstatus, vreason;
  json answer, analyzer, verification, meta;
  Incident* inc;
  int64_t t0;
};

std::string verdict_line(const Report& r) {
  const auto& v = r.verification;
  if (r.vstatus == "passed") return "PASSED: with " + v["matched"].dump() + " disabled the peak 30 s failure rate falls from " + pct(v["rate_all_enabled"].get<double>()) + " to " + pct(v["rate_disabled"].get<double>());
  if (r.vstatus == "failed") return "FAILED: the incident persists (" + pct(v["rate_disabled"].get<double>()) + " vs " + pct(v["rate_all_enabled"].get<double>()) + ") with " + v["matched"].dump() + " disabled";
  return "INCONCLUSIVE: " + r.vreason;
}

std::string markdown(const Report& r) {
  std::ostringstream o;
  const auto& a = r.answer;
  o << "# Incident report: " << r.id << "\n\n";
  o << "**Root cause:** `" << r.category << "` in **" << r.service << "**: " << r.summary << "\n\n";
  o << "**Confidence:** " << r.final_conf << " (model said " << r.llm_conf << "; replay verification " << r.vstatus << ")\n\n";
  o << "## Verification\n\n" << verdict_line(r) << ".\n\nReplay compares a run with every environment condition enabled against a run with the conditions behind this hypothesis disabled. It was given no label saying which condition matters.\n\n";
  const auto& sym = r.analyzer["symptom"];
  o << "## Symptom\n\n" << sym["failed_traces"] << " of " << sym["traces"] << " requests failed at the entry service; the sustained burst starts at " << rel(sym["sustained_onset_us"].get<int64_t>(), r.t0) << " (record `" << sym["sustained_onset_id"].get<std::string>() << "`).\n\n";
  o << "## Timeline (clock-aligned)\n\n| time | service | event | record |\n|---|---|---|---|\n";
  for (auto& e : r.analyzer["timeline"]) o << "| " << rel(e["t_us"].get<int64_t>(), r.t0) << " | " << e["service"].get<std::string>() << " | " << e["text"].get<std::string>() << " | `" << e["id"].get<std::string>() << "` |\n";
  o << "\n## Causal chain\n\n";
  int n = 0;
  for (auto& c : a["causal_chain"]) {
    o << ++n << ". **" << (c["status"] == "confirmed" ? "Confirmed" : "Uncertain") << "**: " << c["claim"].get<std::string>() << "\n";
    for (auto& id : c["cites"]) {
      const std::string line = render_record(*r.inc, id.get<std::string>().substr(id.get<std::string>().find_first_not_of("[ ")), r.t0);
      o << "   - `" << id.get<std::string>() << "`" << (line.empty() ? " (unknown record)" : ": " + line.substr(line.find(']') + 2)) << "\n";
    }
  }
  if (a.contains("alternatives") && !a["alternatives"].empty()) {
    o << "\n## Alternatives considered\n\n";
    for (auto& c : a["alternatives"]) o << "- `" << c.value("category", "") << "` in " << c.value("service", "") << ": " << c.value("why_less_likely", "") << "\n";
  }
  if (!r.analyzer["ruled_out"].empty()) {
    o << "\n## Ruled out\n\n";
    for (auto& x : r.analyzer["ruled_out"]) o << "- " << x["change"].get<std::string>() << " on " << x["service"].get<std::string>() << ": " << x["reason"].get<std::string>() << "\n";
  }
  o << "\n## Analysis layer candidates\n\n| # | category | service | score | coverage | summary |\n|---|---|---|---|---|---|\n";
  for (auto& h : r.analyzer["hypotheses"]) o << "| " << h["rank"] << " | " << h["category"].get<std::string>() << " | " << h["service"].get<std::string>() << " | " << h["score"] << " | " << h["coverage"] << " | " << h["summary"].get<std::string>() << " |\n";
  o << "\n## Method\n\nClocks aligned using causal span pairs (reference `" << r.analyzer["clock"]["reference_service"].get<std::string>() << "`; offsets in ms:";
  for (auto& [k, v] : r.analyzer["clock"]["offsets_us"].items()) o << " " << k << "=" << std::round(v.get<double>() / 100) / 10;
  o << "). Model `" << r.model << "`; " << r.meta["prompt_tokens"] << " prompt / " << r.meta["completion_tokens"] << " completion tokens, ~$" << r.meta["cost_usd"].get<double>() << ", " << r.meta["latency_s"].get<double>() << " s. Every cited record id was checked against the incident's records.\n";
  return o.str();
}

std::string html(const Report& r) {
  std::ostringstream o;
  const auto& a = r.answer;
  const std::string badge = r.vstatus == "passed" ? "ok" : r.vstatus == "failed" ? "bad" : "warn";
  o << "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Traceback: " << esc(r.id) << "</title>"
       "<style>:root{--bg:#fff;--fg:#1b1f24;--mut:#59636e;--line:#d8dee4;--ok:#1a7f37;--bad:#cf222e;--warn:#9a6700;--card:#f6f8fa}"
       "@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--mut:#8b949e;--line:#30363d;--ok:#3fb950;--bad:#f85149;--warn:#d29922;--card:#161b22}}"
       "body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;max-width:900px;margin:0 auto;padding:24px 16px}"
       "h1{font-size:22px}h2{font-size:17px;margin-top:28px;border-bottom:1px solid var(--line);padding-bottom:4px}code{background:var(--card);padding:1px 5px;border-radius:4px;font-size:13px}"
       "table{border-collapse:collapse;width:100%;font-size:13px}td,th{border:1px solid var(--line);padding:4px 8px;text-align:left;vertical-align:top}"
       ".card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 16px}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}"
       "li{margin:4px 0}.rec{color:var(--mut);font-size:12.5px;font-family:ui-monospace,monospace;word-break:break-all}</style><body>";
  o << "<h1>Incident report: " << esc(r.id) << "</h1><div class=card><p><b>Root cause:</b> <code>" << esc(r.category) << "</code> in <b>" << esc(r.service) << "</b>: " << esc(r.summary) << "</p>"
    << "<p><b>Confidence:</b> " << esc(r.final_conf) << " <span class=rec>(model said " << esc(r.llm_conf) << ")</span><br><b>Replay verification:</b> <span class=" << badge << ">" << esc(verdict_line(r)) << "</span></p></div>";
  const auto& sym = r.analyzer["symptom"];
  o << "<h2>Symptom</h2><p>" << sym["failed_traces"] << " of " << sym["traces"] << " requests failed at the entry service; the sustained burst starts at " << rel(sym["sustained_onset_us"].get<int64_t>(), r.t0) << ".</p>";
  o << "<h2>Timeline (clock-aligned)</h2><table><tr><th>time<th>service<th>event<th>record</tr>";
  for (auto& e : r.analyzer["timeline"]) o << "<tr><td>" << rel(e["t_us"].get<int64_t>(), r.t0) << "<td>" << esc(e["service"].get<std::string>()) << "<td>" << esc(e["text"].get<std::string>()) << "<td><code>" << esc(e["id"].get<std::string>()) << "</code></tr>";
  o << "</table><h2>Causal chain</h2><ol>";
  for (auto& c : a["causal_chain"]) {
    o << "<li><b class=" << (c["status"] == "confirmed" ? "ok" : "warn") << ">" << (c["status"] == "confirmed" ? "Confirmed" : "Uncertain") << "</b>: " << esc(c["claim"].get<std::string>()) << "<ul>";
    for (auto& id : c["cites"]) {
      const std::string s = id.get<std::string>();
      const std::string line = render_record(*r.inc, s.substr(s.find_first_not_of("[ ")), r.t0);
      o << "<li class=rec><code>" << esc(s) << "</code> " << esc(line.empty() ? "(unknown record)" : line.substr(line.find(']') + 2)) << "</li>";
    }
    o << "</ul></li>";
  }
  o << "</ol>";
  if (a.contains("alternatives") && !a["alternatives"].empty()) {
    o << "<h2>Alternatives considered</h2><ul>";
    for (auto& c : a["alternatives"]) o << "<li><code>" << esc(c.value("category", "")) << "</code> in " << esc(c.value("service", "")) << ": " << esc(c.value("why_less_likely", "")) << "</li>";
    o << "</ul>";
  }
  if (!r.analyzer["ruled_out"].empty()) {
    o << "<h2>Ruled out</h2><ul>";
    for (auto& x : r.analyzer["ruled_out"]) o << "<li>" << esc(x["change"].get<std::string>()) << " on " << esc(x["service"].get<std::string>()) << ": " << esc(x["reason"].get<std::string>()) << "</li>";
    o << "</ul>";
  }
  o << "<h2>Analysis-layer candidates</h2><table><tr><th>#<th>category<th>service<th>score<th>coverage<th>summary</tr>";
  for (auto& h : r.analyzer["hypotheses"]) o << "<tr><td>" << h["rank"] << "<td>" << esc(h["category"].get<std::string>()) << "<td>" << esc(h["service"].get<std::string>()) << "<td>" << h["score"] << "<td>" << h["coverage"] << "<td>" << esc(h["summary"].get<std::string>()) << "</tr>";
  o << "</table><p class=rec>Model " << esc(r.model) << "; " << r.meta["prompt_tokens"] << " prompt / " << r.meta["completion_tokens"] << " completion tokens; " << r.meta["latency_s"].get<double>() << " s. Every cited record id was checked against the incident's records.</p></body></html>";
  return o.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_investigate <incident_dir> [--replay manifest] [--out dir] [--cache dir] [--model M]\n"); return 2; }
  ReasonOptions o;
  std::string replay, out = "runs/report";
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--replay") replay = v; else if (k == "--out") out = v; else if (k == "--cache") o.llm.cache_dir = v; else if (k == "--model") o.llm.model = v;
    else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
  }
  try {
    Incident inc = load_incident(argv[1]);
    json res = reason(inc, o);
    if (res["answer"].is_null()) { std::fprintf(stderr, "no valid answer from the model: %s\n", res.value("error", "malformed output").c_str()); return 1; }
    Report r{res["incident_id"], res["model"], res["answer"]["root_cause"]["category"], res["answer"]["root_cause"]["service"], res["answer"]["root_cause"]["summary"], res["answer"]["confidence"], "", "", "", res["answer"], res["analyzer"], json::object(), res["meta"], &inc, res["t0_us"]};
    const double observed = observed_peak_failure_rate(inc);
    VerifyResult v = replay.empty() ? VerifyResult{"inconclusive", "no replay manifest given", {}, observed, 0, 0} : verify_hypothesis(replay, observed, r.category, r.service);
    r.vstatus = v.status;
    r.vreason = v.reason;
    r.final_conf = apply_verification(r.llm_conf, v.status);
    r.verification = {{"status", v.status}, {"reason", v.reason}, {"matched", v.matched}, {"rate_observed", v.rate_observed}, {"rate_all_enabled", v.rate_all}, {"rate_disabled", v.rate_disabled}};
    fs::create_directories(out);
    std::ofstream(out + "/report.md") << markdown(r);
    std::ofstream(out + "/report.html") << html(r);
    res["verification"] = r.verification;
    res["final_confidence"] = r.final_conf;
    std::ofstream(out + "/report.json") << res.dump(1);
    std::printf("\n=== %s ===\nroot cause : %s in %s\nsummary    : %s\nconfidence : %s (model: %s, verification: %s)\nverify     : %s\n", r.id.c_str(), r.category.c_str(), r.service.c_str(), r.summary.c_str(), r.final_conf.c_str(), r.llm_conf.c_str(), v.status.c_str(), verdict_line(r).c_str());
    std::printf("report     : %s/report.md, %s/report.html\n", out.c_str(), out.c_str());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
