#include "tb/reasoner.hpp"

#include "tb/analyzer_v2.hpp"
#include "tb/analyzer_v3.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace tb {
namespace {

const std::set<std::string> kCategories = {"dependency_slow", "retry_amplification", "pool_exhaustion", "deploy_regression", "config_regression",
                                           "process_crash", "capacity_saturation", "internal_errors", "edge_unreachable", "other"};

std::string slurp(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string system_prompt(const ReasonOptions& o) {
  std::string s = slurp(o.prompts_dir + "/" + (o.prompt_name.empty() ? o.system : o.prompt_name) + "_system.txt");
  const std::string fmt = slurp(o.prompts_dir + "/output_format.txt");
  const std::string tag = "{{OUTPUT_FORMAT}}";
  const auto p = s.find(tag);
  if (p != std::string::npos) s.replace(p, tag.size(), fmt);
  // The first paragraph describes the system. If the incident supplies its own architecture description, use it instead.
  if (!o.description.empty()) {
    const auto e = s.find("\n\n");
    if (e != std::string::npos) s.replace(0, e, "You are an on-call SRE investigating an incident in " + o.description);
  }
  return s;
}

std::string secs(int64_t us) {
  char b[32];
  std::snprintf(b, sizeof b, "%+.3fs", us / 1e6);
  return b;
}
std::string compact(const json& j) { return j.is_null() || (j.is_object() && j.empty()) ? "" : " " + j.dump(); }

// ---- record rendering. `clock` picks aligned time (traceback) or raw per-service time (baseline). ----
struct Renderer {
  const Incident& inc;
  bool aligned;
  int64_t t0;
  std::string t(int64_t aligned_t, int64_t raw_ts) const { return secs(aligned ? aligned_t - t0 : raw_ts - inc.win_start); }
  // `compact_form`: baseline lines inside a trace group (trace id is the group header, levels abbreviated).
  std::string log(const Log& l, bool compact_form = false) const {
    if (compact_form) {
      std::string s = "[" + l.id + "] " + t(l.t, l.ts) + " " + l.service + " " + std::string(1, static_cast<char>(std::toupper(l.level[0])));
      if (!l.event.empty()) s += " " + l.event;
      s += " \"" + l.msg + "\"";
      return s + compact(l.f);
    }
    std::string s = "[" + l.id + "] " + t(l.t, l.ts) + " log " + l.service + " " + l.level;
    if (!l.event.empty()) s += " event=" + l.event;
    s += " \"" + l.msg + "\"";
    if (!l.trace.empty()) s += " trace=" + l.trace.substr(0, 8);
    return s + compact(l.f);
  }
  std::string span(const Span& x) const {
    std::string s = "[" + x.id + "] " + t(x.t, x.start) + " span " + x.service + " " + x.kind + " " + x.op;
    if (!x.peer.empty()) s += " -> " + x.peer;
    s += " status=" + std::to_string(x.status) + " dur=" + std::to_string(x.dur / 1000) + "ms";
    if (x.queue) s += " queue=" + std::to_string(x.queue / 1000) + "ms";
    if (x.attempt > 1 || x.kind == "client") s += " attempt=" + std::to_string(x.attempt);
    if (!x.error.empty()) s += " error=" + x.error;
    return s + " trace=" + x.trace.substr(0, 8);
  }
  std::string metric(const Metric& m) const {
    char b[256];
    std::snprintf(b, sizeof b, " req=%d err5xx=%d p50=%.0fms p99=%.0fms queue_max=%d busy_max=%d workers_limit=%d pool_in_use_max=%d boots=%d", m.req, m.err5xx, m.p50_ms, m.p99_ms, m.queue_max, m.busy_max, m.workers_limit, m.pool_in_use_max, m.boots);
    return "[" + m.id + "] " + t(m.t, m.ts) + " metric " + m.service + b;
  }
  std::string ops(const Ops& o) const { return "[" + o.id + "] " + t(o.t, o.ts) + " ops " + o.type + " " + o.service + compact(o.detail); }
};

bool lifecycle(const Log& l) { return l.event == "service_started" || l.event == "config_changed" || l.level == "fatal"; }

// ---------------- baseline: raw telemetry, no analysis ----------------
std::string baseline_user(Incident& inc, int budget, json& meta) {
  Renderer R{inc, false, 0};
  std::vector<const Log*> keep, must;
  for (auto& l : inc.logs) {
    if (lifecycle(l)) must.push_back(&l);
    else if (l.level == "warn" || l.level == "error") keep.push_back(&l);
  }
  std::string fixed = "Operations events:\n";
  std::vector<const Ops*> ops;
  for (auto& o : inc.ops) ops.push_back(&o);
  std::sort(ops.begin(), ops.end(), [](auto a, auto b) { return a->ts < b->ts; });
  for (auto o : ops) fixed += R.ops(*o) + "\n";
  fixed += "\nService metrics (one line per service per 5s window; times are per-service clock):\n";
  std::vector<const Metric*> ms;
  for (auto& m : inc.metrics) ms.push_back(&m);
  std::sort(ms.begin(), ms.end(), [](auto a, auto b) { return a->service != b->service ? a->service < b->service : a->ts < b->ts; });
  for (auto m : ms) fixed += R.metric(*m) + "\n";
  size_t chars = fixed.size();
  for (auto l : must) chars += R.log(*l, true).size() + 1;
  size_t total_keep = keep.size();
  // Estimated prompt tokens. 2.6 chars/token was measured on a real call (72.7k tokens for 192k chars).
  auto est_for = [&](double p, std::vector<const Log*>* sel) {
    size_t c = chars;
    std::vector<const Log*> picked;
    for (auto l : keep) {
      bool take = l->trace.empty();
      if (!take) {
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char ch : l->trace) { h ^= ch; h *= 1099511628211ULL; }
        take = (h % 10000) < static_cast<uint64_t>(p * 10000);
      }
      if (take) { c += R.log(*l, true).size() + 3; picked.push_back(l); }
    }
    if (sel) *sel = picked;
    return static_cast<int>(c / 2.6);
  };
  std::vector<const Log*> sel;
  const int est_full = est_for(1.0, &sel);
  double keep_p = 1.0;
  bool truncated = false;
  if (est_full > budget) {
    // Whole trace groups are dropped (deterministic hash sample) so each shown trace stays coherent.
    truncated = true;
    while (keep_p > 0.01 && est_for(keep_p, &sel) > budget) keep_p *= 0.97;
  }
  size_t groups_total = 0, groups_kept = 0;
  { std::set<std::string> all, kept; for (auto l : keep) if (!l->trace.empty()) all.insert(l->trace); for (auto l : sel) if (!l->trace.empty()) kept.insert(l->trace); groups_total = all.size(); groups_kept = kept.size(); }
  sel.insert(sel.end(), must.begin(), must.end());
  // group by trace id (groups ordered by first raw timestamp), then ungrouped lines
  std::map<std::string, std::vector<const Log*>> by_trace;
  std::vector<const Log*> loose;
  for (auto l : sel) (l->trace.empty() ? loose : by_trace[l->trace]).push_back(l);
  std::vector<std::pair<int64_t, std::string>> order;
  for (auto& [tr, v] : by_trace) {
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->ts < b->ts; });
    order.push_back({v.front()->ts, tr});
  }
  std::sort(order.begin(), order.end());
  std::sort(loose.begin(), loose.end(), [](auto a, auto b) { return a->ts < b->ts; });
  std::string out = "INCIDENT WINDOW: " + std::to_string(inc.services.size()) + " services. Times are seconds since the window start on each service's own clock.\n\n" + fixed + "\nLogs (warn/error and lifecycle only), grouped by trace id (level W=warn E=error F=fatal I=info), groups ordered by first timestamp, lines by timestamp:\n";
  for (auto& [ts, tr] : order) {
    out += "trace " + tr.substr(0, 8) + ":\n";
    for (auto l : by_trace[tr]) out += " " + R.log(*l, true) + "\n";
  }
  if (truncated) out += "\nNOTE: the telemetry was too large for the context window, so only a uniformly random " + std::to_string(static_cast<int>(keep_p * 100)) + "% of trace groups is shown above (" + std::to_string(groups_kept) + " of " + std::to_string(groups_total) + "). Lines with no trace id, lifecycle logs, metrics and ops events are complete.\n";
  out += "\nLogs with no trace id:\n";
  for (auto l : loose) out += R.log(*l, true) + "\n";
  meta["baseline"] = {{"warn_error_logs_total", total_keep}, {"warn_error_logs_included", sel.size() - must.size()}, {"lifecycle_logs", must.size()}, {"est_prompt_tokens_full", est_full}, {"truncated", truncated}, {"trace_groups_total", groups_total}, {"trace_groups_included", groups_kept}, {"kept_fraction", keep_p}};
  return out;
}

// ---------------- traceback: analyzer candidates + evidence ----------------
std::string traceback_user(Incident& inc, const json& rep, json& meta) {
  int64_t t0 = INT64_MAX;
  for (auto& s : inc.spans) t0 = std::min(t0, s.t);
  Renderer R{inc, true, t0};
  std::ostringstream o;
  const auto& sym = rep["symptom"];
  o << "INCIDENT: " << sym["traces"] << " traces, " << sym["failed_traces"] << " failed at the entry service";
  if (sym["failed_traces"].get<int>() > 0) {
    if (sym["sustained_burst_found"].get<bool>())
      o << "; the sustained burst of failures starts at " << secs(sym["sustained_onset_us"].get<int64_t>() - t0) << " (record " << sym["sustained_onset_id"].get<std::string>() << "); " << sym["failures_before_burst"] << " isolated failures occurred before it (background noise)";
    else
      o << " (first failure " << secs(sym["first_failure_t_us"].get<int64_t>() - t0) << ", record " << sym["first_failure_id"].get<std::string>() << ")";
  }
  o << ".\nTimes below are on one aligned clock (seconds since the first span). Reference clock: " << rep["clock"]["reference_service"].get<std::string>() << "; estimated offsets (ms):";
  for (auto& [k, v] : rep["clock"]["offsets_us"].items()) o << " " << k << "=" << std::round(v.get<double>() / 100) / 10;
  o << "\n\nDEPENDENCY GRAPH (calls, failed, timeouts, refused, 5xx errors, retried attempts):\n";
  for (auto& e : rep["graph"]) o << "  " << e["caller"].get<std::string>() << " -> " << e["callee"].get<std::string>() << ": " << e["calls"] << ", " << e["failed"] << ", " << e["timeouts"] << ", " << e["refused"] << ", " << e["errors"] << ", " << e["retried_attempts"] << "\n";
  o << "\nCANDIDATE CAUSES (ranked by the analysis layer; score in 0..1; coverage = share of failed traces the candidate touches):\n";
  std::set<std::string> want;
  int k = 0;
  for (auto& h : rep["hypotheses"]) {
    if (++k > 6) break;
    o << " " << h["hyp_id"].get<std::string>() << " category=" << h["category"].get<std::string>() << " service=" << h["service"].get<std::string>();
    if (!h["edge"].is_null()) o << " edge=" << h["edge"]["from"].get<std::string>() << "->" << h["edge"]["to"].get<std::string>();
    if (h["onset_us"].get<int64_t>() > 0) o << " onset=" << secs(h["onset_us"].get<int64_t>() - t0);
    o << " score=" << h["score"] << " coverage=" << h["coverage"] << "\n   summary: " << h["summary"].get<std::string>() << "\n   features: " << h["features"].dump() << "\n   evidence:";
    for (auto& e : h["evidence"]) { o << " " << e["id"].get<std::string>() << " (" << e["note"].get<std::string>() << ");"; want.insert(e["id"]); }
    o << "\n";
    int extra = 0;
    for (auto& id : h["support_ids"]) if (!want.count(id.get<std::string>()) && extra++ < 6) want.insert(id.get<std::string>());
  }
  o << "\nRULED OUT (changes that were not followed by failures):\n";
  for (auto& r : rep["ruled_out"]) {
    o << "  " << r["change"].get<std::string>() << " on " << r["service"].get<std::string>() << " at " << secs(r["t_us"].get<int64_t>() - t0) << ": " << r["reason"].get<std::string>() << " [";
    for (auto& id : r["ids"]) { o << id.get<std::string>() << " "; want.insert(id.get<std::string>()); }
    o << "]\n";
  }
  if (rep["ruled_out"].empty()) o << "  (none)\n";
  o << "\nTIMELINE (aligned clock):\n";
  for (auto& e : rep["timeline"]) {
    o << "  " << secs(e["t_us"].get<int64_t>() - t0) << " " << e["kind"].get<std::string>() << " " << e["service"].get<std::string>() << ": " << e["text"].get<std::string>() << " [" << e["id"].get<std::string>() << "]\n";
    want.insert(e["id"].get<std::string>());
  }
  o << "\nAnalysis-layer heuristic confidence in its top candidate: " << rep["confidence"].get<std::string>() << " (a heuristic, not a fact).\n\nRECORDS (the only ids you may cite):\n";
  std::map<std::string, std::string> text;
  for (auto& l : inc.logs) if (want.count(l.id)) text[l.id] = R.log(l);
  for (auto& s : inc.spans) if (want.count(s.id)) text[s.id] = R.span(s);
  for (auto& m : inc.metrics) if (want.count(m.id)) text[m.id] = R.metric(m);
  for (auto& x : inc.ops) if (want.count(x.id)) text[x.id] = R.ops(x);
  for (auto& [id, line] : text) o << line << "\n";
  meta["records_provided"] = text.size();
  return o.str();
}

}  // namespace

json extract_json(const std::string& text) {
  const auto a = text.find('{'), b = text.rfind('}');
  if (a == std::string::npos || b == std::string::npos || b < a) return nullptr;
  json j = json::parse(text.substr(a, b - a + 1), nullptr, false);
  return j.is_discarded() ? json(nullptr) : j;
}

namespace {
std::string sget(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
}  // namespace

std::string validate_answer_shape(const json& a) {
  if (!a.is_object()) return "the answer is not a JSON object";
  if (!a.contains("root_cause") || !a["root_cause"].is_object()) return "missing object root_cause";
  const auto& rc = a["root_cause"];
  if (!rc.contains("category") || !rc["category"].is_string() || !kCategories.count(rc["category"].get<std::string>())) return "root_cause.category must be exactly one of the listed categories";
  if (!rc.contains("service") || !rc["service"].is_string()) return "root_cause.service must be a string";
  if (!a.contains("confidence") || !a["confidence"].is_string() || !std::set<std::string>{"high", "medium", "low"}.count(a["confidence"].get<std::string>())) return "confidence must be high, medium or low";
  if (!a.contains("causal_chain") || !a["causal_chain"].is_array() || a["causal_chain"].empty()) return "causal_chain must be a non-empty array";
  for (auto& c : a["causal_chain"]) {
    if (!c.is_object() || !c.contains("claim") || !c["claim"].is_string()) return "each causal_chain item needs a string claim";
    if (!c.contains("status") || !c["status"].is_string() || !std::set<std::string>{"confirmed", "uncertain"}.count(c["status"].get<std::string>())) return "each claim needs status confirmed or uncertain";
    if (!c.contains("cites") || !c["cites"].is_array() || c["cites"].empty()) return "each claim must cite at least one record id";
    for (auto& id : c["cites"]) if (!id.is_string()) return "cites must be strings";
  }
  if (a.contains("alternatives")) {
    if (!a["alternatives"].is_array()) return "alternatives must be an array";
    for (auto& c : a["alternatives"]) {
      if (!c.is_object()) return "each alternative must be an object";
      for (const char* k : {"category", "service", "why_less_likely"}) if (c.contains(k) && !c[k].is_string()) return std::string("alternative field ") + k + " must be a string";
      if (c.contains("cites")) {
        if (!c["cites"].is_array()) return "alternative cites must be an array";
        for (auto& id : c["cites"]) if (!id.is_string()) return "cites must be strings";
      }
    }
  }
  return "";
}

// Tolerate surrounding brackets/whitespace only ("[orders-L1]" -> "orders-L1"). Whole lines are not accepted.
std::string normalize_id(std::string id) {
  while (!id.empty() && (id.front() == '[' || id.front() == ' ')) id.erase(id.begin());
  while (!id.empty() && (id.back() == ']' || id.back() == ' ')) id.pop_back();
  return id;
}

std::vector<Claim> claims_of(const json& a) {
  std::vector<Claim> out;
  auto add = [&](const json& c, const std::string& text) {
    Claim cl;
    cl.text = text;
    cl.status = sget(c, "status").empty() ? "uncertain" : sget(c, "status");
    if (c.contains("cites") && c["cites"].is_array()) for (auto& id : c["cites"]) if (id.is_string()) cl.cites.push_back(normalize_id(id.get<std::string>()));
    out.push_back(cl);
  };
  if (a.contains("causal_chain") && a["causal_chain"].is_array()) for (auto& c : a["causal_chain"]) if (c.is_object()) add(c, sget(c, "claim"));
  if (a.contains("alternatives") && a["alternatives"].is_array()) for (auto& c : a["alternatives"]) if (c.is_object()) add(c, sget(c, "why_less_likely"));
  return out;
}


// v2 override rule: the answer may differ from the analyzer's top candidate (H1) only by citing records that contradict it.
// Mechanically, a contradicting record must exist, be about H1's own service (or an endpoint of its edge), fall inside H1's
// time window, and not be part of H1's own supporting evidence. Returns "" if the answer is acceptable.
std::string override_problem(const Incident& inc, const json& a, const json& rep) {
  const auto& top = rep["hypotheses"][0];
  const auto& rc = a["root_cause"];
  if (rc["category"] == top["category"] && rc["service"] == top["service"]) return "";
  const std::string chose = rc["category"].get<std::string>() + "/" + rc["service"].get<std::string>();
  const std::string deflt = top["category"].get<std::string>() + "/" + top["service"].get<std::string>();
  const std::string head = "You answered " + chose + " instead of the analysis layer's top candidate " + deflt + " (H1)";
  if (!a.contains("override") || !a["override"].is_object() || !a["override"].contains("contradicting_records") || !a["override"]["contradicting_records"].is_array() || a["override"]["contradicting_records"].empty())
    return head + " without an \"override\" field. Either confirm H1, or add \"override\": {\"contradicting_records\": [...], \"why\": \"...\"} listing records about H1's service that contradict H1";
  std::set<std::string> sup, svcs = {top["service"].get<std::string>()};
  for (auto& id : top["support_ids"]) sup.insert(id.get<std::string>());
  if (!top["edge"].is_null()) { svcs.insert(top["edge"]["from"].get<std::string>()); svcs.insert(top["edge"]["to"].get<std::string>()); }
  const int64_t t0 = top["onset_us"].get<int64_t>() - 5000000, t1 = top["end_us"].get<int64_t>() + 5000000;
  auto where = [&](const std::string& id, std::string& svc, int64_t& t) {
    for (auto& l : inc.logs) if (l.id == id) { svc = l.service; t = l.t; return true; }
    for (auto& x : inc.spans) if (x.id == id) { svc = x.service; t = x.t; return true; }
    for (auto& m : inc.metrics) if (m.id == id) { svc = m.service; t = m.t; return true; }
    for (auto& o : inc.ops) if (o.id == id) { svc = o.service; t = o.t; return true; }
    return false;
  };
  for (auto& jid : a["override"]["contradicting_records"]) {
    if (!jid.is_string()) return head + ", but contradicting_records must be strings";
    const std::string id = normalize_id(jid.get<std::string>());
    if (sup.count(id)) return head + ", but " + id + " is part of H1's own supporting evidence, so it cannot contradict H1. Cite records about " + top["service"].get<std::string>() + " that contradict H1, or confirm H1";
    std::string svc;
    int64_t t = 0;
    if (!where(id, svc, t)) continue;  // unknown ids are reported by the citation check
    if (!svcs.count(svc)) return head + ", but " + id + " is a record of " + svc + ", not of H1's service " + top["service"].get<std::string>() + ". A record contradicts H1 only if it shows that H1's own service did not behave as H1 claims. Cite such records, or confirm H1";
    if (t < t0 || t > t1) return head + ", but " + id + " lies outside the time window of H1. Cite records from H1's window, or confirm H1";
  }
  return "";
}

std::string read_description(const std::string& dir) {
  std::ifstream in(dir + "/meta.json");
  if (!in) return "";
  json m = json::parse(in, nullptr, false);
  return m.is_object() && m.contains("description") && m["description"].is_string() ? m["description"].get<std::string>() : "";
}

std::string render_record(const Incident& inc, const std::string& id, int64_t t0) {
  Renderer R{inc, true, t0};
  for (auto& l : inc.logs) if (l.id == id) return R.log(l);
  for (auto& x : inc.spans) if (x.id == id) return R.span(x);
  for (auto& m : inc.metrics) if (m.id == id) return R.metric(m);
  for (auto& o : inc.ops) if (o.id == id) return R.ops(o);
  return "";
}

json reason(Incident& inc, const ReasonOptions& opt) {
  json meta = json::object();
  json rep;
  std::string user;
  if (opt.system == "traceback") {
    rep = opt.rules == "v3" ? analyze_v3(inc, opt.incident_dir) : opt.rules == "v2" ? analyze_v2(inc) : analyze(inc);  // aligns clocks in place
    user = traceback_user(inc, rep, meta);
  } else {
    user = baseline_user(inc, opt.token_budget, meta);
  }
  const std::set<std::string> valid = inc.all_ids();
  std::set<std::string> support;  // analyzer support (traceback only)
  Messages msgs = {{"system", system_prompt(opt)}, {"user", user}};
  if (opt.dry_run) return {{"incident_id", inc.id}, {"system", msgs[0].second}, {"user", user}, {"est_tokens", (msgs[0].second.size() + user.size()) / 3.2}, {"meta", meta}};

  json out = {{"incident_id", inc.id}, {"system", opt.system}, {"model", opt.llm.model}};
  json attempts = json::array();
  json answer = nullptr;
  int pt = 0, ct = 0;
  double lat = 0;
  bool accepted = false;
  for (int attempt = 0; attempt <= opt.max_retries; ++attempt) {
    LlmResult r = chat(opt.llm, msgs);
    pt += r.prompt_tokens;
    ct += r.completion_tokens;
    lat += r.latency_s;
    if (!r.ok) { attempts.push_back({{"error", r.error}}); out["error"] = r.error; break; }
    json a = extract_json(r.content);
    std::string problem = a.is_null() ? "your reply did not contain a valid JSON object" : validate_answer_shape(a);
    json at = {{"cached", r.cached}, {"prompt_tokens", r.prompt_tokens}, {"completion_tokens", r.completion_tokens}, {"raw", r.content}};
    std::string feedback;
    if (!problem.empty()) {
      at["problem"] = problem;
      feedback = "Your previous answer was rejected: " + problem + ". Answer again with only the JSON object in the required format.";
    } else {
      std::vector<Claim> claims = claims_of(a);
      if (opt.rules != "v1" && a.contains("override") && a["override"].is_object() && a["override"].contains("contradicting_records") && a["override"]["contradicting_records"].is_array()) {
        Claim oc;
        oc.text = "override";
        for (auto& id : a["override"]["contradicting_records"]) if (id.is_string()) oc.cites.push_back(normalize_id(id.get<std::string>()));
        claims.push_back(oc);
      }
      CitationCheck cc = validate_citations(claims, valid, valid);
      at["cites_total"] = cc.total;
      at["cites_invalid"] = cc.invalid;
      if (!cc.ok()) {
        std::string ids;
        for (auto& id : cc.invalid) ids += (ids.empty() ? "" : ", ") + id;
        feedback = "Your previous answer was rejected because it cites record ids that do not exist in the data: " + ids + ". Cite only ids exactly as they appear in the data, and re-answer with only the JSON object.";
        answer = a;  // keep the last well-formed answer in case retries run out
      } else if (const std::string op = opt.rules != "v1" ? override_problem(inc, a, rep) : ""; !op.empty()) {
        at["problem"] = op;
        feedback = "Your previous answer was rejected: " + op + ". Answer again with only the JSON object in the required format.";
        answer = a;  // kept in case retries run out; replaced by the analyzer's top candidate below
      } else {
        answer = a;
        accepted = true;
      }
    }
    attempts.push_back(at);
    if (accepted) break;
    msgs.push_back({"assistant", r.content});
    msgs.push_back({"user", feedback});
  }
  if (opt.rules != "v1" && !answer.is_null() && !override_problem(inc, answer, rep).empty()) {
    // The model kept differing from the analyzer without contradicting records: the analyzer's top candidate stands.
    const auto& top = rep["hypotheses"][0];
    out["override_rejected"] = {{"model_answer", answer["root_cause"]}, {"model_confidence", answer["confidence"]}, {"reason", override_problem(inc, answer, rep)}};
    answer["root_cause"] = {{"category", top["category"]}, {"service", top["service"]}, {"summary", top["summary"]}};
    answer["confidence"] = rep["confidence"];
    accepted = true;
  }
  out["answer"] = answer;
  out["accepted"] = accepted;  // false: no attempt produced a well-formed answer with only existing ids
  if (opt.system == "traceback" && !answer.is_null()) {
    for (auto& h : rep["hypotheses"]) for (auto& id : h["support_ids"]) support.insert(id.get<std::string>());
    int64_t t0 = INT64_MAX;
    for (auto& sp : inc.spans) t0 = std::min(t0, sp.t);
    out["t0_us"] = t0;
    out["analyzer"] = rep;
    out["analyzer_top"] = {{"category", rep["hypotheses"][0]["category"]}, {"service", rep["hypotheses"][0]["service"]}, {"confidence", rep["confidence"]}};
    json sup = json::array();
    for (auto& h : rep["hypotheses"]) sup.push_back({{"category", h["category"]}, {"service", h["service"]}, {"support_ids", h["support_ids"]}});
    out["analyzer_support"] = sup;
  }
  meta["attempts"] = attempts;
  meta["prompt_tokens"] = pt;
  meta["completion_tokens"] = ct;
  meta["latency_s"] = lat;
  meta["cost_usd"] = pt * opt.price_in + ct * opt.price_out;
  out["meta"] = meta;
  return out;
}

}  // namespace tb
