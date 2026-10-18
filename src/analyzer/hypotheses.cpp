#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

#include "tb/analyzer.hpp"

// Deterministic root-cause hypothesis generation. Nothing here knows about the simulator:
// rules work on spans (causal structure, status, durations, retries), service metrics
// (capacity, queues, pools, restarts), change events (deploy/config/restart) and logs.
namespace tb {
namespace {

constexpr int64_t kSec = 1000000;

struct Burst {
  bool found = false;
  int64_t onset = 0, end = 0;
  int n = 0;
};

// First dense cluster: >= k events within `win`, extended while gaps stay <= `gap`.
Burst find_burst(std::vector<int64_t> t, int k = 4, int64_t win = 5 * kSec, int64_t gap = 6 * kSec) {
  Burst b;
  std::sort(t.begin(), t.end());
  for (size_t i = 0; i + k - 1 < t.size(); ++i) {
    if (t[i + k - 1] - t[i] > win) continue;
    size_t j = i + k - 1;
    while (j + 1 < t.size() && t[j + 1] - t[j] <= gap) ++j;
    b = {true, t[i], t[j], static_cast<int>(j - i + 1)};
    return b;
  }
  return b;
}

double quantile(std::vector<double> v, double q) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(q * v.size()))];
}

struct Hyp {
  std::string category, service, edge_from, edge_to, summary;
  int64_t onset = 0, end = 0;
  double strength = 0, coverage = 0, score = 0;
  std::vector<std::pair<std::string, std::string>> ev;  // (id, role/note)
  std::set<int> anom;                                   // anomalous span indices
  std::vector<std::string> extra_support;               // more supporting ids (not shown as evidence)
  json features = json::object();

  void cite(const std::string& id, const std::string& note) {
    for (auto& e : ev) if (e.first == id) return;
    ev.push_back({id, note});
  }
};

struct Change {
  std::string kind, service;  // deploy | config | restart
  int64_t t = 0;
  std::vector<std::string> ids;
  json detail = json::object();
};

class Analysis {
 public:
  Analysis(Incident& inc) : inc_(inc) {}

  json run() {
    cm_ = estimate_clocks(inc_.spans);
    apply_clocks(inc_, cm_);
    cor_ = correlate(inc_);
    graph_ = build_graph(inc_);
    index();
    baseline();
    leaves();
    anomalies();
    metric_features();
    changes();
    std::vector<Hyp> hs;
    rule_crash(hs);
    rule_pool(hs);
    rule_capacity(hs);
    rule_retry(hs);
    rule_changes(hs);
    rule_slow(hs);
    rule_errors(hs);
    rule_unreachable(hs);
    suppress(hs);
    score(hs);
    return report(hs);
  }

 private:
  using Times = std::vector<int64_t>;
  const Span& S(int i) const { return inc_.spans[i]; }

  // ---- indexing ----
  void index() {
    for (size_t i = 0; i < inc_.spans.size(); ++i) {
      t_min_ = std::min(t_min_, S(i).t);
      t_max_ = std::max(t_max_, S(i).t);
    }
    for (size_t i = 0; i < cor_.traces.size(); ++i)
      if (cor_.traces[i].failed) ++n_failed_;
  }
  bool is_fail(int i) const {
    const Span& s = S(i);
    return s.kind == "server" ? s.status >= 500 : (s.status <= 0 || s.status >= 500);
  }
  // time spent in the span itself: not queueing, not waiting on children
  int64_t excl(int i) const {
    int64_t d = S(i).dur - S(i).queue;
    for (int c : cor_.children[i]) d -= S(c).dur;
    return std::max<int64_t>(d, 0);
  }

  // ---- baseline from windows with no failing root trace ----
  void baseline() {
    std::set<int64_t> bad_bins;
    for (auto& tr : cor_.traces)
      if (tr.failed && tr.root >= 0) bad_bins.insert((S(tr.root).t - t_min_) / (5 * kSec));
    std::map<std::string, std::vector<double>> self, queue;
    auto collect = [&](bool only_quiet) {
      self.clear();
      queue.clear();
      for (size_t i = 0; i < inc_.spans.size(); ++i) {
        const Span& s = S(i);
        if (s.kind != "server" || s.status >= 500) continue;
        if (only_quiet && bad_bins.count((s.t - t_min_) / (5 * kSec))) continue;
        self[s.service].push_back(excl(i) / 1000.0);
        queue[s.service].push_back(s.queue / 1000.0);
      }
    };
    collect(true);
    size_t n = 0;
    for (auto& [k, v] : self) n += v.size();
    if (n < 100) collect(false);
    for (auto& [svc, v] : self) {
      base_p50_[svc] = quantile(v, 0.5);
      base_p95_[svc] = quantile(v, 0.95);
      base_q95_[svc] = quantile(queue[svc], 0.95);
    }
  }

  // ---- leaf failures: where failure originates in each trace ----
  void leaves() {
    for (size_t i = 0; i < inc_.spans.size(); ++i) {
      if (!is_fail(static_cast<int>(i))) continue;
      bool child_fail = false;
      for (int c : cor_.children[i]) if (is_fail(c)) child_fail = true;
      if (child_fail) continue;
      const Span& s = S(i);
      if (s.kind == "server") {
        err_leaf_[s.service].push_back(static_cast<int>(i));
      } else {
        bool has_server_child = !cor_.children[i].empty();
        if (s.status < 0) refused_[s.peer].push_back(static_cast<int>(i));
        else if (s.status == 0) (has_server_child ? tmo_exec_ : tmo_noserver_)[s.peer].push_back(static_cast<int>(i));
        else err_from_[s.peer].push_back(static_cast<int>(i));
      }
    }
  }

  void anomalies() {
    for (size_t i = 0; i < inc_.spans.size(); ++i) {
      const Span& s = S(i);
      if (s.kind != "server") continue;
      const double ex = excl(static_cast<int>(i)) / 1000.0, q = s.queue / 1000.0;
      if (s.status < 500 && ex > std::max(3 * base_p95_[s.service], 50.0)) slow_[s.service].push_back(static_cast<int>(i));
      if (q > std::max(50.0, 10 * base_q95_[s.service])) queued_[s.service].push_back(static_cast<int>(i));
    }
  }

  Times times_of(const std::vector<int>& idx, int64_t from = INT64_MIN) const {
    Times t;
    for (int i : idx) if (S(i).t >= from) t.push_back(S(i).t);
    return t;
  }
  std::vector<int> filter(const std::vector<int>& idx, int64_t from, int64_t to) const {
    std::vector<int> o;
    for (int i : idx) if (S(i).t >= from && S(i).t <= to) o.push_back(i);
    return o;
  }

  // ---- metrics-derived features ----
  struct MetricFeat {
    bool wl_drop = false, boots_inc = false, gap = false, pool_sat = false;
    int64_t wl_drop_t = 0, boots_t = 0, gap_from = 0, gap_to = 0, pool_t = 0;
    std::string wl_id, boots_id, gap_id_a, gap_id_b, pool_id;
    int wl_nominal = 0, wl_min = 0, pool_max = 0;
  };
  void metric_features() {
    std::map<std::string, std::vector<const Metric*>> by;
    for (auto& m : inc_.metrics) by[m.service].push_back(&m);
    for (auto& [svc, v] : by) {
      std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->t < b->t; });
      MetricFeat f;
      f.wl_nominal = 0;
      f.wl_min = INT32_MAX;
      for (auto m : v) { f.wl_nominal = std::max(f.wl_nominal, m->workers_limit); f.wl_min = std::min(f.wl_min, m->workers_limit); }
      if (f.wl_nominal > 0 && f.wl_min * 2 <= f.wl_nominal)
        for (auto m : v) if (m->workers_limit * 2 <= f.wl_nominal) { f.wl_drop = true; f.wl_drop_t = m->t; f.wl_id = m->id; break; }
      for (size_t i = 1; i < v.size(); ++i) {
        if (v[i]->boots > v[i - 1]->boots && !f.boots_inc) { f.boots_inc = true; f.boots_t = v[i]->t; f.boots_id = v[i]->id; }
        if (v[i]->t - v[i - 1]->t > 7500000 && !f.gap) {
          f.gap = true; f.gap_from = v[i - 1]->t; f.gap_to = v[i]->t; f.gap_id_a = v[i - 1]->id; f.gap_id_b = v[i]->id;
        }
      }
      // pool gauge pinned at its maximum at the end of the window after starting well below it
      int mx = 0;
      for (auto m : v) mx = std::max(mx, m->pool_in_use_max);
      f.pool_max = mx;
      if (mx >= 3 && v.size() >= 8) {
        size_t q = v.size() / 4;
        std::vector<double> head;
        for (size_t i = 0; i < q; ++i) head.push_back(v[i]->pool_in_use_max);
        const double h = quantile(head, 0.5);
        size_t tail_ok = 0;
        for (size_t i = v.size() - 3; i < v.size(); ++i) if (v[i]->pool_in_use_max == mx) ++tail_ok;
        if (tail_ok == 3 && mx - h >= 3)
          for (auto m : v) if (m->pool_in_use_max == mx) { f.pool_sat = true; f.pool_t = m->t; f.pool_id = m->id; break; }
      }
      mf_[svc] = f;
    }
  }

  // ---- change events (deploy / config / restart) ----
  void changes() {
    auto near_boot = [&](const std::string& svc, int64_t t) {
      auto it = mf_.find(svc);
      return it != mf_.end() && it->second.boots_inc && std::llabs(it->second.boots_t - t) < 12 * kSec;
    };
    std::set<std::string> used_ops;
    for (auto& l : inc_.logs) {
      if (l.event != "config_changed" && l.event != "service_started") continue;
      Change c;
      c.service = l.service;
      c.t = l.t;
      c.ids.push_back(l.id);
      c.detail = l.f;
      if (l.event == "config_changed") c.kind = "config";
      for (auto& o : inc_.ops) {
        if (o.service != l.service || std::llabs(o.t - l.t) > 2 * kSec) continue;
        if (l.event == "config_changed" && o.type == "config_change") { c.ids.push_back(o.id); used_ops.insert(o.id); }
        if (l.event == "service_started" && o.type == "deploy" && o.detail.value("phase", "") == "finished") { c.kind = "deploy"; c.ids.push_back(o.id); used_ops.insert(o.id); c.detail = o.detail; }
        if (l.event == "service_started" && o.type == "container_restarted") { c.kind = "restart"; c.ids.push_back(o.id); used_ops.insert(o.id); }
      }
      if (l.event == "service_started" && c.kind.empty()) c.kind = near_boot(l.service, l.t) ? "restart" : "deploy";
      changes_.push_back(c);
    }
    // ops deploy "started" events: attach to the matching deploy
    for (auto& o : inc_.ops)
      if (o.type == "deploy" && o.detail.value("phase", "") == "started")
        for (auto& c : changes_) if (c.kind == "deploy" && c.service == o.service && c.t >= o.t && c.t - o.t < 10 * kSec) c.ids.insert(c.ids.begin(), o.id);
    for (auto& o : inc_.ops)
      if (o.type == "deploy" && o.detail.value("phase", "") == "rollback") rollbacks_.push_back(&o);
  }

  // ---- evidence helpers ----
  void add_spans(Hyp& h, const std::vector<int>& idx, const char* role, size_t n = 3) {
    std::vector<int> v = idx;
    std::sort(v.begin(), v.end(), [&](int a, int b) { return S(a).t < S(b).t; });
    for (size_t i = 0; i < v.size() && i < n; ++i) h.cite(S(v[i]).id, role);
    for (int i : v) h.anom.insert(i);
  }
  void add_logs(Hyp& h, const std::string& svc, int64_t from, int64_t to, const std::function<bool(const Log&)>& pred, const char* role, size_t n = 3) {
    std::vector<const Log*> v;
    for (auto& l : inc_.logs) if (l.service == svc && l.t >= from && l.t <= to && pred(l)) v.push_back(&l);
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->t < b->t; });
    for (size_t i = 0; i < v.size(); ++i) {
      if (i < n) h.cite(v[i]->id, role);
      else if (h.extra_support.size() < 40) h.extra_support.push_back(v[i]->id);
    }
  }
  static bool is_problem(const Log& l) { return l.level == "error" || l.level == "warn" || l.level == "fatal"; }

  // ---- rules ----
  void rule_crash(std::vector<Hyp>& hs) {
    for (auto& svc : inc_.services) {
      auto rf = refused_.find(svc);
      Burst rb = rf == refused_.end() ? Burst{} : find_burst(times_of(rf->second), 3);
      const MetricFeat& f = mf_[svc];
      if (!rb.found && !f.gap) continue;
      bool fatal = false;
      for (auto& l : inc_.logs) if (l.service == svc && l.level == "fatal") fatal = true;
      Hyp h;
      h.category = "process_crash";
      h.service = svc;
      h.strength = std::min(0.98, (rb.found ? 0.5 : 0.0) + (f.gap ? 0.25 : 0.0) + (f.boots_inc ? 0.15 : 0.0) + (fatal ? 0.15 : 0.0));
      if (h.strength < 0.45) continue;
      h.onset = rb.found ? rb.onset : f.gap_from;
      h.end = f.gap ? f.gap_to : rb.end;
      if (rb.found) add_spans(h, rf->second, "connection refused calling the service");
      auto nf = tmo_noserver_.find(svc);
      if (nf != tmo_noserver_.end()) add_spans(h, filter(nf->second, h.onset - 2 * kSec, h.end + 5 * kSec), "call timed out and never reached the service", 0);
      add_logs(h, svc, h.onset - 3 * kSec, h.end + 15 * kSec, [](const Log& l) { return l.level == "fatal" || l.event == "service_started"; }, "service lifecycle log");
      if (f.gap) { h.cite(f.gap_id_a, "last metric sample before metrics went silent"); h.cite(f.gap_id_b, "first metric sample after the gap"); }
      if (f.boots_inc) h.cite(f.boots_id, "boot counter increased");
      h.features = {{"refused_calls", rb.n}, {"metrics_gap_s", f.gap ? (f.gap_to - f.gap_from) / 1e6 : 0.0}, {"boot_counter_increased", f.boots_inc}, {"fatal_log", fatal}};
      h.summary = svc + " stopped answering (refused connections" + (f.gap ? ", metrics gap" : "") + (f.boots_inc ? ", restarted" : "") + ")";
      hs.push_back(std::move(h));
    }
  }

  void rule_pool(std::vector<Hyp>& hs) {
    for (auto& svc : inc_.services) {
      Times lt;
      std::vector<const Log*> logs;
      for (auto& l : inc_.logs) if (l.service == svc && (l.event.find("pool") != std::string::npos)) { lt.push_back(l.t); logs.push_back(&l); }
      Burst lb = find_burst(lt, 4);
      const MetricFeat& f = mf_[svc];
      std::vector<int> pspans;
      for (int i : err_leaf_[svc]) if (S(i).error.find("pool") != std::string::npos) pspans.push_back(i);
      if (!lb.found && !f.pool_sat) continue;
      Hyp h;
      h.category = "pool_exhaustion";
      h.service = svc;
      h.strength = std::min(0.97, (lb.found ? 0.75 : 0.0) + (f.pool_sat ? 0.2 : 0.0) + (!pspans.empty() ? 0.05 : 0.0));
      if (!lb.found) h.strength = 0.5;
      // Pool waits are bounded by the wait timeout. Successful requests that took far longer than a
      // pool-exhausted failure did cannot be explained by pool waiting: service time itself inflated.
      std::vector<double> fail_ms, slow_ms;
      for (int i : pspans) fail_ms.push_back(excl(i) / 1000.0);
      for (int i : slow_[svc]) slow_ms.push_back(excl(i) / 1000.0);
      Burst sb = find_burst(times_of(slow_[svc]), 4);
      const bool inflated = sb.found && !fail_ms.empty() && quantile(slow_ms, 0.5) > 2 * quantile(fail_ms, 0.5);
      if (inflated) h.strength *= 0.3;
      h.onset = lb.found ? lb.onset : f.pool_t;
      h.end = lb.found ? lb.end : t_max_;
      add_spans(h, pspans, "request failed with pool exhaustion");
      add_logs(h, svc, h.onset - 1 * kSec, t_max_, [](const Log& l) { return l.event.find("pool") != std::string::npos; }, "pool exhaustion log");
      if (f.pool_sat) h.cite(f.pool_id, "pool gauge pinned at its maximum");
      h.features = {{"service_time_inflated_beyond_pool_wait", inflated}, {"pool_log_burst", lb.n}, {"pool_gauge_saturated", f.pool_sat}, {"pool_gauge_max", f.pool_max}};
      h.summary = svc + " ran out of database connections";
      hs.push_back(std::move(h));
    }
  }

  void rule_capacity(std::vector<Hyp>& hs) {
    for (auto& svc : inc_.services) {
      const MetricFeat& f = mf_[svc];
      Burst qb = find_burst(times_of(queued_[svc]), 4);
      if (!f.wl_drop && !qb.found) continue;
      bool pinned = false;
      for (auto& m : inc_.metrics) if (m.service == svc && m.workers_limit > 0 && m.busy_max >= m.workers_limit && m.t > (qb.found ? qb.onset : f.wl_drop_t)) pinned = true;
      Hyp h;
      h.category = "capacity_saturation";
      h.service = svc;
      // Without a drop in the worker limit, queueing is as likely an effect (slow service times
      // hold workers longer) as a cause, so it stays a weak hypothesis.
      h.strength = f.wl_drop ? 0.7 + (qb.found ? 0.25 : 0.0) : (pinned ? 0.4 : 0.3);
      h.onset = qb.found ? qb.onset : f.wl_drop_t - 5 * kSec;
      h.end = qb.found ? qb.end : t_max_;
      add_spans(h, queued_[svc], "request waited in queue before being served");
      if (f.wl_drop) h.cite(f.wl_id, "worker limit dropped from " + std::to_string(f.wl_nominal) + " to " + std::to_string(f.wl_min));
      for (auto& m : inc_.metrics) if (m.service == svc && qb.found && m.queue_max > 20 && m.t > qb.onset) { h.cite(m.id, "queue depth high"); break; }
      h.features = {{"workers_nominal", f.wl_nominal}, {"workers_min", f.wl_min}, {"queued_requests", qb.n}, {"workers_pinned", pinned}};
      h.summary = svc + " lost serving capacity; requests queue behind too few workers";
      hs.push_back(std::move(h));
    }
  }

  void rule_retry(std::vector<Hyp>& hs) {
    // Group client attempts into logical calls: (trace, parent span, callee).
    struct Call { std::vector<int> attempts; };
    std::map<std::tuple<std::string, std::string, std::string>, Call> calls;
    for (size_t i = 0; i < inc_.spans.size(); ++i)
      if (S(i).kind == "client" && !S(i).peer.empty()) calls[{S(i).trace, S(i).parent, S(i).peer}].attempts.push_back(static_cast<int>(i));
    std::map<std::pair<std::string, std::string>, std::vector<const Call*>> deep;  // edge -> calls with >=4 attempts
    std::map<std::pair<std::string, std::string>, std::vector<int64_t>> gaps;
    for (auto& [k, c] : calls) {
      auto v = c.attempts;
      std::sort(v.begin(), v.end(), [&](int a, int b) { return S(a).t < S(b).t; });
      if (v.size() < 4) continue;
      auto edge = std::make_pair(S(v[0]).service, S(v[0]).peer);
      deep[edge].push_back(&c);
      for (size_t i = 1; i < v.size(); ++i) gaps[edge].push_back(S(v[i]).t - (S(v[i - 1]).t + S(v[i - 1]).dur));
    }
    for (auto& [edge, cs] : deep) {
      if (cs.size() < 5) continue;
      auto g = gaps[edge];
      std::sort(g.begin(), g.end());
      const int64_t gap_med = g[g.size() / 2];
      std::vector<int> heavy;
      for (auto c : cs) for (int i : c->attempts) heavy.push_back(i);
      Hyp h;
      h.category = "retry_amplification";
      h.service = edge.first;
      h.edge_from = edge.first;
      h.edge_to = edge.second;
      h.strength = gap_med < 20000 ? 0.9 : 0.45;
      h.onset = INT64_MAX;
      for (int i : heavy) h.onset = std::min(h.onset, S(i).t);
      h.end = t_max_;
      add_spans(h, heavy, "retry attempt in a call that was retried 4+ times");
      add_logs(h, edge.first, h.onset - 5 * kSec, t_max_, [&](const Log& l) { return l.event == "retry_scheduled" && l.f.value("backoff_ms", int64_t{-1}) == 0; }, "retry scheduled with zero backoff");
      h.features = {{"calls_retried_4plus", static_cast<int>(cs.size())}, {"median_gap_between_attempts_ms", gap_med / 1000.0}};
      h.summary = edge.first + " retries calls to " + edge.second + " repeatedly without backoff, amplifying load on " + edge.second;
      hs.push_back(std::move(h));
    }
  }

  void rule_changes(std::vector<Hyp>& hs) {
    for (auto& c : changes_) {
      if (c.kind == "restart") continue;
      if (c.kind == "deploy") {
        std::vector<int> err = err_leaf_[c.service], slow = slow_[c.service], from_ = err_from_[c.service];
        err.insert(err.end(), from_.begin(), from_.end());
        Burst eb = find_burst(times_of(filter(err, c.t - 3 * kSec, c.t + 45 * kSec)), 4);
        Burst sb = find_burst(times_of(filter(slow, c.t - 3 * kSec, c.t + 45 * kSec)), 4);
        if (!eb.found && !sb.found) { ruled_out_.push_back({c, "no errors or slowdown at " + c.service + " after the change"}); continue; }
        Hyp h;
        h.category = "deploy_regression";
        h.service = c.service;
        const int64_t first = eb.found && sb.found ? std::min(eb.onset, sb.onset) : (eb.found ? eb.onset : sb.onset);
        h.onset = first;
        h.end = std::max(eb.found ? eb.end : 0, sb.found ? sb.end : 0);
        const double lag = (first - c.t) / 1e6;
        h.strength = lag <= 15 ? 0.92 : 0.7;
        for (auto& id : c.ids) h.cite(id, "deploy of " + c.service);
        add_spans(h, filter(err, c.t - 3 * kSec, c.t + 45 * kSec), "failure after the deploy");
        add_spans(h, filter(slow, c.t - 3 * kSec, c.t + 45 * kSec), "slow request after the deploy", 2);
        for (auto* r : rollbacks_) if (r->service == c.service && r->t > c.t) h.cite(r->id, "deploy rolled back");
        h.features = {{"seconds_from_change_to_first_failure", lag}, {"errors_after_change", eb.n}, {"slow_after_change", sb.n}};
        h.summary = c.service + " began failing " + std::to_string(static_cast<int>(lag)) + "s after a deploy";
        hs.push_back(std::move(h));
      } else if (c.kind == "config") {
        // Calls from this service that time out after the change while the callee answered fine.
        std::map<std::string, std::vector<int>> by_callee;
        for (auto& [callee, idx] : tmo_exec_) for (int i : idx) if (S(i).service == c.service && S(i).t >= c.t - kSec && S(i).t <= c.t + 60 * kSec) by_callee[callee].push_back(i);
        bool any = false;
        for (auto& [callee, idx] : by_callee) {
          Burst b = find_burst(times_of(idx), 4);
          if (!b.found) continue;
          std::vector<double> client_ms, callee_ms;
          for (int i : idx) {
            client_ms.push_back(S(i).dur / 1000.0);
            for (int ch : cor_.children[i]) if (S(ch).status < 500) callee_ms.push_back(excl(ch) / 1000.0);
          }
          const double cm = quantile(client_ms, 0.5), km = callee_ms.empty() ? 0 : quantile(callee_ms, 0.5);
          Hyp h;
          h.category = "config_regression";
          h.service = c.service;
          h.edge_from = c.service;
          h.edge_to = callee;
          h.strength = (!callee_ms.empty() && cm < km * 4 + 20) ? 0.92 : 0.75;
          h.onset = b.onset;
          h.end = b.end;
          for (auto& id : c.ids) h.cite(id, "configuration change in " + c.service);
          add_spans(h, idx, "client gave up on a call the callee still served");
          h.features = {{"key", c.detail.value("key", "")}, {"timeouts_after_change", b.n}, {"client_wait_ms_median", cm}, {"callee_self_ms_median", km}, {"callee_healthy", !callee_ms.empty()}};
          h.summary = c.service + " began timing out calls to " + callee + " right after a config change, though " + callee + " was answering";
          hs.push_back(std::move(h));
          any = true;
        }
        if (!any) ruled_out_.push_back({c, "no timeouts followed the config change"});
      }
    }
  }

  void rule_slow(std::vector<Hyp>& hs) {
    for (auto& svc : inc_.services) {
      Burst b = find_burst(times_of(slow_[svc]), 4);
      if (!b.found) continue;
      Hyp h;
      h.category = "dependency_slow";
      h.service = svc;
      int timeouts = 0;
      for (int i : tmo_exec_[svc]) if (S(i).t >= b.onset - 2 * kSec && S(i).t <= b.end + 5 * kSec) ++timeouts;
      h.strength = 0.72 + (timeouts >= 4 ? 0.12 : 0.0);
      h.onset = b.onset;
      h.end = b.end;
      add_spans(h, slow_[svc], "request served far slower than baseline");
      add_spans(h, tmo_exec_[svc], "caller timed out waiting for this service", 2);
      add_logs(h, svc, b.onset - 2 * kSec, b.end + 5 * kSec, [](const Log& l) { return is_problem(l); }, "warning/error log", 2);
      h.features = {{"slow_requests", b.n}, {"caller_timeouts", timeouts}, {"baseline_self_p95_ms", base_p95_[svc]}};
      h.summary = svc + " responses became much slower than baseline; callers timed out";
      hs.push_back(std::move(h));
    }
  }

  void rule_errors(std::vector<Hyp>& hs) {
    for (auto& svc : inc_.services) {
      // Only failures the service itself recorded. Client-side 5xx with no server span are mostly
      // rejections by an overloaded service (shedding), a consequence rather than an origin.
      std::vector<int> idx = err_leaf_[svc];
      Burst b = find_burst(times_of(idx), 4);
      if (!b.found) continue;
      // Lost spans make a few failures look like origins; require a real share of the service's traffic.
      int served = 0, origin = 0;
      for (size_t i = 0; i < inc_.spans.size(); ++i) if (S(i).kind == "server" && S(i).service == svc && S(i).t >= b.onset && S(i).t <= b.end) ++served;
      for (int i : idx) if (S(i).t >= b.onset && S(i).t <= b.end) ++origin;
      if (origin < 0.05 * std::max(served, 1)) continue;
      Hyp h;
      h.category = "internal_errors";
      h.service = svc;
      h.strength = 0.7;
      h.onset = b.onset;
      h.end = b.end;
      std::map<std::string, int> codes;
      for (int i : err_leaf_[svc]) ++codes[S(i).error];
      add_spans(h, idx, "failure originating in this service");
      add_logs(h, svc, b.onset - 2 * kSec, b.end + 5 * kSec, [](const Log& l) { return l.level == "error"; }, "error log", 2);
      json cj = json::object();
      for (auto& [k, v] : codes) cj[k] = v;
      h.features = {{"error_origin_spans", b.n}, {"error_codes", cj}};
      h.summary = "failures originate in " + svc + "; callers surface them as generic errors";
      hs.push_back(std::move(h));
    }
  }

  void rule_unreachable(std::vector<Hyp>& hs) {
    for (auto& [callee, idx] : tmo_noserver_) {
      std::map<std::string, std::vector<int>> by_caller;
      for (int i : idx) by_caller[S(i).service].push_back(i);
      for (auto& [caller, v] : by_caller) {
        Burst b = find_burst(times_of(v), 4);
        if (!b.found) continue;
        Hyp h;
        h.category = "edge_unreachable";
        h.service = callee;
        h.edge_from = caller;
        h.edge_to = callee;
        h.strength = 0.55;
        h.onset = b.onset;
        h.end = b.end;
        add_spans(h, v, "call timed out without the callee ever recording it");
        h.features = {{"unanswered_calls", b.n}};
        h.summary = "calls from " + caller + " to " + callee + " vanish without reaching it";
        hs.push_back(std::move(h));
      }
    }
  }

  // ---- combine ----
  void suppress(std::vector<Hyp>& hs) {
    auto specific = [](const std::string& c) {
      return c == "process_crash" || c == "pool_exhaustion" || c == "capacity_saturation" || c == "deploy_regression" || c == "config_regression" || c == "retry_amplification";
    };
    // A downstream saturation explained by an upstream retry policy is not the root.
    for (auto& h : hs)
      if (h.category == "capacity_saturation")
        for (auto& r : hs) if (r.category == "retry_amplification" && r.edge_to == h.service && r.strength >= 0.5 && !mf_[h.service].wl_drop) h.strength *= 0.35;
    for (auto& h : hs) {
      if (specific(h.category)) continue;
      for (auto& s : hs) {
        if (!specific(s.category) || s.strength < 0.5) continue;
        const bool same = s.service == h.service || (s.category == "retry_amplification" && s.edge_to == h.service) || (s.category == "config_regression" && s.edge_to == h.service);
        if (same && s.category != "capacity_saturation") { h.strength *= 0.3; break; }
        if (same && s.category == "capacity_saturation") { h.strength *= 0.3; break; }
      }
    }
  }

  void score(std::vector<Hyp>& hs) {
    std::set<int> failed_traces;
    for (size_t i = 0; i < cor_.traces.size(); ++i) if (cor_.traces[i].failed) failed_traces.insert(static_cast<int>(i));
    for (auto& h : hs) {
      std::set<int> t;
      for (int i : h.anom) t.insert(cor_.trace_of.at(S(i).trace));
      int hit = 0;
      for (int x : t) if (failed_traces.count(x)) ++hit;
      h.coverage = failed_traces.empty() ? 0.0 : static_cast<double>(hit) / failed_traces.size();
      h.score = h.strength * (0.6 + 0.4 * h.coverage);
    }
    std::stable_sort(hs.begin(), hs.end(), [](const Hyp& a, const Hyp& b) { return a.score > b.score; });
  }

  // ---- report ----
  json report(std::vector<Hyp>& hs) {
    json r;
    r["incident_id"] = inc_.id;
    r["clock"] = {{"reference_service", cm_.reference}, {"method", "median(server.start - client.start) over causal span pairs"}, {"offsets_us", cm_.offset_us}, {"edge_samples", cm_.edge_samples}, {"ops_clock_offset_us", cm_.ops_offset_us}, {"ops_clock_calibrated", cm_.ops_calibrated}};
    // Isolated early failures are background noise. The symptom onset is the start of the first
    // sustained burst of failing entry requests; the very first failure is reported separately.
    int64_t first_fail = INT64_MAX;
    std::string first_fail_id;
    std::vector<std::pair<int64_t, std::string>> fails;
    for (auto& tr : cor_.traces)
      if (tr.failed && tr.root >= 0) fails.push_back({S(tr.root).t, S(tr.root).id});
    std::sort(fails.begin(), fails.end());
    if (!fails.empty()) { first_fail = fails.front().first; first_fail_id = fails.front().second; }
    Times ft;
    for (auto& f : fails) ft.push_back(f.first);
    const Burst sb = find_burst(ft, 8);
    int64_t onset = first_fail;
    std::string onset_id = first_fail_id;
    int before = 0;
    if (sb.found) {
      onset = sb.onset;
      for (auto& f : fails) if (f.first == sb.onset) { onset_id = f.second; break; }
      for (auto& f : fails) if (f.first < sb.onset) ++before;
    }
    r["symptom"] = {{"traces", static_cast<int>(cor_.traces.size())}, {"failed_traces", n_failed_}, {"first_failure_t_us", n_failed_ ? first_fail : 0}, {"first_failure_id", first_fail_id},
                    {"sustained_burst_found", sb.found}, {"sustained_onset_us", n_failed_ ? onset : 0}, {"sustained_onset_id", onset_id}, {"failures_before_burst", before}};
    json edges = json::array();
    for (auto& e : graph_) edges.push_back({{"caller", e.caller}, {"callee", e.callee}, {"calls", e.calls}, {"failed", e.fail}, {"timeouts", e.timeouts}, {"refused", e.refused}, {"errors", e.errors}, {"retried_attempts", e.retries}});
    r["graph"] = edges;

    if (hs.empty()) {
      Hyp u;
      u.category = "unexplained";
      u.strength = 0.1;
      u.score = 0.05;
      u.summary = "no rule found a localized anomaly";
      hs.push_back(u);
    }
    const double top = hs[0].score, second = hs.size() > 1 ? hs[1].score : 0.0;
    const char* conf = (top >= 0.65 && top - second >= 0.2) ? "high" : top >= 0.45 ? "medium" : "low";
    r["confidence"] = conf;

    json jh = json::array();
    int rank = 0;
    for (auto& h : hs) {
      json ev = json::array();
      std::set<std::string> support;
      for (auto& [id, note] : h.ev) { ev.push_back({{"id", id}, {"note", note}}); support.insert(id); }
      for (auto& id : h.extra_support) support.insert(id);
      std::vector<int> an(h.anom.begin(), h.anom.end());
      std::sort(an.begin(), an.end(), [&](int a, int b) { return S(a).t < S(b).t; });
      for (size_t i = 0; i < an.size() && i < 60; ++i) support.insert(S(an[i]).id);
      jh.push_back({{"rank", ++rank}, {"hyp_id", "H" + std::to_string(rank)}, {"category", h.category}, {"service", h.service}, {"edge", h.edge_from.empty() ? json(nullptr) : json{{"from", h.edge_from}, {"to", h.edge_to}}},
                    {"onset_us", h.onset}, {"end_us", h.end}, {"strength", std::round(h.strength * 1000) / 1000}, {"coverage", std::round(h.coverage * 1000) / 1000}, {"score", std::round(h.score * 1000) / 1000},
                    {"summary", h.summary}, {"features", h.features}, {"evidence", ev}, {"support_ids", support}});
    }
    r["hypotheses"] = jh;

    json ro = json::array();
    for (auto& [c, why] : ruled_out_) ro.push_back({{"change", c.kind}, {"service", c.service}, {"t_us", c.t}, {"ids", c.ids}, {"reason", why}});
    r["ruled_out"] = ro;

    // Timeline of key events on the aligned clock.
    struct TL { int64_t t; std::string id, kind, service, text; };
    std::vector<TL> tl;
    for (auto& c : changes_) tl.push_back({c.t, c.ids.back(), c.kind + "_change", c.service, c.kind + " event on " + c.service});
    if (n_failed_) tl.push_back({onset, onset_id, "symptom_onset", cm_.reference, std::string(sb.found ? "start of the sustained burst of failing entry requests" : "first failing entry request")});
    for (size_t k = 0; k < hs.size() && k < 4; ++k) {
      auto& h = hs[k];
      if (h.ev.empty() || h.category == "unexplained") continue;
      tl.push_back({h.onset, h.ev.front().first, "anomaly_onset", h.service, "H" + std::to_string(k + 1) + " " + h.category + ": " + h.summary});
    }
    for (auto& [svc, f] : mf_) {
      if (f.wl_drop) tl.push_back({f.wl_drop_t, f.wl_id, "metric", svc, "worker limit dropped"});
      if (f.gap) tl.push_back({f.gap_from, f.gap_id_a, "metric", svc, "metrics went silent"});
      if (f.boots_inc) tl.push_back({f.boots_t, f.boots_id, "metric", svc, "boot counter increased"});
    }
    std::sort(tl.begin(), tl.end(), [](auto& a, auto& b) { return a.t < b.t; });
    json jt = json::array();
    for (auto& e : tl) jt.push_back({{"t_us", e.t}, {"t_rel_s", (e.t - t_min_) / 1e6}, {"id", e.id}, {"kind", e.kind}, {"service", e.service}, {"text", e.text}});
    r["timeline"] = jt;
    return r;
  }

  Incident& inc_;
  ClockModel cm_;
  Correlated cor_;
  std::vector<Edge> graph_;
  int64_t t_min_ = INT64_MAX, t_max_ = INT64_MIN;
  int n_failed_ = 0;
  std::map<std::string, double> base_p50_, base_p95_, base_q95_;
  std::map<std::string, std::vector<int>> err_leaf_, err_from_, refused_, tmo_exec_, tmo_noserver_, slow_, queued_;
  std::map<std::string, MetricFeat> mf_;
  std::vector<Change> changes_;
  std::vector<const Ops*> rollbacks_;
  std::vector<std::pair<Change, std::string>> ruled_out_;
};

}  // namespace

json analyze(Incident& inc) { return Analysis(inc).run(); }

}  // namespace tb
