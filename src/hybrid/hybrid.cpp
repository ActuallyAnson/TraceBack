#include "tb/hybrid.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "tb/analyzer_v3.hpp"

namespace tb {

std::string normalize_id(std::string id);  // defined in src/llm/reasoner.cpp (not exported in the frozen header)

namespace {

const std::set<std::string> kCats = {"dependency_slow", "retry_amplification", "pool_exhaustion", "deploy_regression", "config_regression",
                                     "process_crash", "capacity_saturation", "internal_errors", "edge_unreachable", "other"};

std::string sget(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
int rank_of(const std::string& c) { return c == "high" ? 2 : c == "medium" ? 1 : 0; }
std::string pct(double x) { return std::to_string(static_cast<int>(std::lround(x * 100))) + "%"; }

bool is_fail(const Span& s) { return s.kind == "server" ? s.status >= 500 : (s.status <= 0 || s.status >= 500); }

// Per failed entry request, the services where its failure originates (failing leaves of the call tree; a failed call originates at its callee).
std::map<std::string, double> origin_share(const Incident& inc, const Correlated& cor) {
  std::map<std::string, int> count;
  int n_failed = 0;
  for (const Trace& tr : cor.traces) {
    if (!tr.failed || tr.root < 0) continue;
    ++n_failed;
    std::set<std::string> origins;
    std::vector<int> stack = {tr.root};
    while (!stack.empty()) {
      const int i = stack.back();
      stack.pop_back();
      bool child_fail = false;
      for (int c : cor.children[i])
        if (is_fail(inc.spans[c])) { child_fail = true; stack.push_back(c); }
      if (child_fail) continue;
      const Span& s = inc.spans[i];
      origins.insert(s.kind == "server" || s.peer.empty() ? s.service : s.peer);
    }
    for (auto& o : origins) ++count[o];
  }
  std::map<std::string, double> share;
  for (auto& [k, v] : count) share[k] = n_failed ? static_cast<double>(v) / n_failed : 0.0;
  return share;
}

struct Rec { std::string svc, peer; bool anomalous; };
std::map<std::string, Rec> index_records(const Incident& inc) {
  std::map<std::string, Rec> m;
  for (auto& l : inc.logs) m[l.id] = {l.service, "", l.level == "warn" || l.level == "error" || l.level == "fatal"};
  for (auto& s : inc.spans) m[s.id] = {s.service, s.peer, is_fail(s) || !s.error.empty()};
  for (auto& x : inc.metrics) m[x.id] = {x.service, "", x.err5xx > 0};
  for (auto& o : inc.ops) m[o.id] = {o.service, "", true};  // a change event is notable by itself
  return m;
}

void add_unique(std::vector<std::string>& v, const std::string& id) {
  if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
}

}  // namespace

std::string min_conf(const std::string& a, const std::string& b) { return rank_of(a) <= rank_of(b) ? a : b; }

std::string support_ceiling(double origin, double anomalous) { return (origin >= 0.8 && anomalous >= 0.8) ? "high" : (origin >= 0.5 && anomalous >= 0.5) ? "medium" : "low"; }

std::vector<HybridCand> propose_from_answer(const json& ans, const std::set<std::string>& valid, const std::vector<std::string>& services, json& rejected) {
  std::vector<HybridCand> out;
  if (!ans.is_object() || !ans.contains("root_cause") || !ans["root_cause"].is_object()) return out;
  struct Raw { std::string cat, svc, text; std::vector<std::string> cites; bool top; };
  std::vector<Raw> raw;
  {
    Raw r{sget(ans["root_cause"], "category"), sget(ans["root_cause"], "service"), sget(ans["root_cause"], "summary"), {}, true};
    if (ans.contains("causal_chain") && ans["causal_chain"].is_array())
      for (auto& c : ans["causal_chain"]) {
        if (r.text.empty()) r.text = sget(c, "claim");
        if (c.is_object() && c.contains("cites") && c["cites"].is_array()) for (auto& id : c["cites"]) if (id.is_string()) add_unique(r.cites, normalize_id(id.get<std::string>()));
      }
    raw.push_back(r);
  }
  if (ans.contains("alternatives") && ans["alternatives"].is_array())
    for (auto& a : ans["alternatives"]) {
      if (!a.is_object()) continue;
      Raw r{sget(a, "category"), sget(a, "service"), sget(a, "why_less_likely"), {}, false};
      if (a.contains("cites") && a["cites"].is_array()) for (auto& id : a["cites"]) if (id.is_string()) add_unique(r.cites, normalize_id(id.get<std::string>()));
      raw.push_back(r);
    }
  std::set<std::pair<std::string, std::string>> seen;
  int taken = 0;
  for (auto& r : raw) {
    if (!kCats.count(r.cat)) r.cat = "other";
    if (r.svc.empty() || !seen.insert({r.cat, r.svc}).second) continue;
    if (++taken > 3) break;
    std::string why;
    std::vector<std::string> bad;
    if (std::find(services.begin(), services.end(), r.svc) == services.end()) why = "service is not in the incident";
    else if (r.cites.empty()) why = "cites no record";
    else for (auto& id : r.cites) if (!valid.count(id)) bad.push_back(id);
    if (why.empty() && !bad.empty()) why = "cites ids that do not exist";
    if (!why.empty()) { rejected.push_back({{"category", r.cat}, {"service", r.svc}, {"reason", why}, {"invalid_ids", bad}}); continue; }
    HybridCand c;
    c.category = r.cat; c.service = r.svc; c.text = r.text; c.cites = r.cites; c.from_llm = true; c.llm_top = r.top;
    out.push_back(c);
  }
  return out;
}

json hybrid(const std::string& dir, const ReasonOptions& bopts) {
  // 1. The model reads the same evidence as the raw-logs baseline (same request, so the same cached answer).
  ReasonOptions bo = bopts;
  bo.system = "baseline";
  Incident inc_b = load_incident(dir);
  json base = reason(inc_b, bo);
  const json& ans = base["answer"];

  // 2. Deterministic layer on a fresh copy (analyzer v3 = v2 plus the real-telemetry normalisations; identical to v2 on simulator data).
  Incident inc = load_incident(dir);
  json rep = analyze_v3(inc, dir);
  const Correlated cor = correlate(inc);
  const auto share = origin_share(inc, cor);
  const auto idx = index_records(inc);
  const std::set<std::string> valid = inc.all_ids();

  json rejected = json::array();
  std::vector<HybridCand> cands = propose_from_answer(ans, valid, inc.services, rejected);
  const int n_llm = static_cast<int>(cands.size());

  // 3. Merge with the analyzer's own candidates (same category and service = one candidate with both sources).
  int k = 0;
  for (auto& h : rep["hypotheses"]) {
    if (++k > 6) break;
    const std::string cat = h["category"].get<std::string>(), svc = h["service"].get<std::string>();
    if (cat == "unexplained" || svc.empty()) continue;
    std::vector<std::string> ev;
    for (auto& e : h["evidence"]) add_unique(ev, e["id"].get<std::string>());
    HybridCand* hit = nullptr;
    for (auto& c : cands) if (c.category == cat && c.service == svc) hit = &c;
    if (!hit) { cands.emplace_back(); hit = &cands.back(); hit->category = cat; hit->service = svc; hit->text = h["summary"].get<std::string>(); hit->cites = ev; }
    else for (auto& id : ev) add_unique(hit->cites, id);
    hit->from_analyzer = true;
    hit->analyzer_rank = k;
    hit->analyzer_score = h["score"].get<double>();
    if (!h["edge"].is_null()) { hit->edge_from = h["edge"]["from"].get<std::string>(); hit->edge_to = h["edge"]["to"].get<std::string>(); }
  }

  // 4. Evidence for every candidate, whoever proposed it: the share of failed entry requests whose failure originates in its service, and
  //    the share of its cited (or, for the analyzer, evidence) records that are anomalous records about it. support = their mean.
  for (auto& c : cands) {
    std::set<std::string> names = {c.service};
    if (!c.edge_from.empty()) { names.insert(c.edge_from); names.insert(c.edge_to); }
    if (share.count(c.service)) c.origin = share.at(c.service);  // for an edge hypothesis the analyzer names the caller
    int good = 0, n = 0;
    for (auto& id : c.cites) {
      auto it = idx.find(id);
      if (it == idx.end()) continue;
      ++n;
      if (it->second.anomalous && (names.count(it->second.svc) || names.count(it->second.peer))) ++good;
    }
    c.anomalous = n ? static_cast<double>(good) / n : 0.0;
    c.score = 0.5 * c.origin + 0.5 * c.anomalous;
  }

  // 5. Choice. The analyzer's top candidate stands unless a model proposal for another service beats it on measured evidence:
  //    it originates at least half of the failed entry requests, at least half of its cited records are anomalous records about it,
  //    and the analyzer's top candidate originates under 20% of them. The model cannot overturn the analyzer by argument alone.
  int a1 = -1, chosen = -1;
  for (size_t i = 0; i < cands.size(); ++i) if (cands[i].analyzer_rank == 1) a1 = static_cast<int>(i);
  bool challenged = false;
  for (size_t i = 0; i < cands.size(); ++i) {
    const HybridCand& c = cands[i];
    if (!c.from_llm || (a1 >= 0 && c.service == cands[a1].service)) continue;
    if (c.origin < 0.5 || c.anomalous < 0.5 || (a1 >= 0 && cands[a1].origin >= 0.2)) continue;
    if (chosen < 0 || c.origin > cands[chosen].origin || (c.origin == cands[chosen].origin && c.llm_top && !cands[chosen].llm_top)) chosen = static_cast<int>(i);
  }
  if (chosen >= 0) {
    challenged = true;
    // keep the analyzer's own category for that service if it raised one (its categories come from measured features)
    int pick = -1;
    for (size_t i = 0; i < cands.size(); ++i)
      if (cands[i].from_analyzer && cands[i].service == cands[chosen].service && (pick < 0 || cands[i].analyzer_score > cands[pick].analyzer_score)) pick = static_cast<int>(i);
    if (pick >= 0) chosen = pick;
  } else if (a1 >= 0) {
    chosen = a1;
  } else if (!cands.empty()) {
    for (size_t i = 0; i < cands.size(); ++i) if (chosen < 0 || cands[i].score > cands[chosen].score) chosen = static_cast<int>(i);
  }
  if (chosen > 0) std::rotate(cands.begin(), cands.begin() + chosen, cands.begin() + chosen + 1);
  const auto rest = cands.begin() + (cands.empty() ? 0 : 1);
  std::stable_sort(rest, cands.end(), [](const HybridCand& a, const HybridCand& b) { return a.score > b.score; });

  json out = {{"incident_id", inc.id}, {"system", "hybrid"}, {"model", base.value("model", std::string())}};
  json jc = json::array();
  for (auto& c : cands) {
    jc.push_back({{"category", c.category}, {"service", c.service}, {"llm", c.from_llm}, {"llm_top", c.llm_top}, {"analyzer_rank", c.analyzer_rank}, {"analyzer_score", c.analyzer_score},
                  {"origin_share", std::round(c.origin * 1000) / 1000}, {"anomalous_cite_share", std::round(c.anomalous * 1000) / 1000}, {"score", std::round(c.score * 1000) / 1000}, {"cites", c.cites}});
  }
  out["hybrid"] = {{"llm_candidates", n_llm}, {"rejected", rejected}, {"candidates", jc}};
  out["meta"] = base["meta"];  // the model cost is the baseline's: the proposals are its answer
  out["accepted"] = !cands.empty();
  if (cands.empty()) {
    out["answer"] = {{"root_cause", {{"category", "other"}, {"service", "none"}, {"summary", "no candidate"}}}, {"confidence", "low"}, {"causal_chain", json::array()}};
    return out;
  }

  // 6. Confidence: never above what the evidence supports, the analyzer's confidence, or the model's confidence in its own answer;
  //    capped at medium when the model's top proposal names a different service than the one chosen.
  const HybridCand& best = cands[0];
  const bool backed = best.from_analyzer && best.analyzer_rank == 1;
  std::string ceiling = backed ? rep["confidence"].get<std::string>() : support_ceiling(best.origin, best.anomalous);
  if (best.from_analyzer && !backed) ceiling = min_conf(ceiling, "medium");
  std::string source_conf = "high";
  if (best.llm_top && ans.is_object()) source_conf = min_conf(source_conf, sget(ans, "confidence"));
  bool disagree = false;
  for (auto& c : cands) if (c.llm_top && c.service != best.service) disagree = true;
  if (disagree) source_conf = min_conf(source_conf, "medium");
  if (source_conf.empty()) source_conf = "low";
  const std::string conf = min_conf(ceiling, source_conf);

  json chain = json::array();
  chain.push_back({{"claim", best.text.empty() ? best.category + " in " + best.service : best.text}, {"status", "uncertain"}, {"cites", best.cites}});
  json alts = json::array();
  for (size_t i = 1; i < cands.size() && i < 3; ++i)
    alts.push_back({{"category", cands[i].category}, {"service", cands[i].service}, {"why_less_likely", "lower evidence score " + pct(cands[i].score) + " vs " + pct(best.score)}, {"cites", cands[i].cites}});
  out["answer"] = {{"root_cause", {{"category", best.category}, {"service", best.service}, {"summary", best.text}}}, {"confidence", conf}, {"causal_chain", chain}, {"alternatives", alts}};
  out["hybrid"]["chosen"] = {{"source", std::string(best.from_llm ? "llm" : "") + (best.from_llm && best.from_analyzer ? "+" : "") + (best.from_analyzer ? "analyzer" : "")},
                             {"support_ceiling", ceiling}, {"source_confidence", source_conf}, {"challenged_analyzer", challenged}, {"model_disagrees", disagree}, {"model_confidence", ans.is_object() ? sget(ans, "confidence") : std::string()}};
  return out;
}

}  // namespace tb
