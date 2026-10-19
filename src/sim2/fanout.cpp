#include "fanout.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <queue>

#include "tb/jsonw.hpp"
#include "tb/rng.hpp"

namespace tb::sim2 {

using sim::Fault;
using sim::FaultKind;

const char* const kSvcName[NSVC] = {"api", "ingest", "broker", "billing", "shipping", "notifier"};
const char* const kDescription =
    "a small event-driven backend with six services: api (entry point), ingest, broker (a message broker), billing, shipping and notifier. "
    "Api calls ingest; ingest publishes each order to the broker; the broker delivers it to billing and shipping in parallel and waits for both, "
    "and also notifies notifier without waiting.";

namespace {

constexpr int64_t kMs = 1000, kSec = 1000000;
constexpr int64_t kDurationUs = sim::kDurationUs;
constexpr double kRps = 20;
constexpr size_t kQueueCap = 150;
const double kBaseMs[NSVC] = {2, 6, 4, 20, 15, 10};
const int kWorkers[NSVC] = {64, 16, 24, 12, 12, 8};
const int kPool[NSVC] = {0, 0, 0, 8, 0, 0};

struct EdgeCfg { int64_t timeout_us; int retries; int64_t backoff_us; };
struct Req { std::string trace, parent_span, op; int caller = -1; int attempt = 1; bool poison = false; };
using Reply = std::function<void(int, const std::string&)>;
using Done = std::function<void(int, const std::string&)>;
struct Pool { struct W { std::shared_ptr<bool> done; std::function<void(bool)> cb; }; int inuse = 0; std::deque<W> waiters; };
struct Pending { Req req; Reply reply; int64_t enq; };
struct Win { int n = 0, err = 0, qmax = 0, busymax = 0, poolmax = 0; std::vector<int64_t> durs; };
struct SvcState {
  bool up = true; int gen = 0; int busy = 0; int boots = 0; int64_t boot_t = -1000 * kSec; std::string version = "v1.0.0";
  std::deque<Pending> q; Pool pool; Win win; int64_t offset_us = 0; uint64_t log_n = 0, span_n = 0; std::string logs, spans, metrics;
};
struct Ev { int64_t t; uint64_t seq; std::function<void()> fn; bool operator>(const Ev& o) const { return t != o.t ? t > o.t : seq > o.seq; } };

std::string hex(uint64_t v, int w) { char b[32]; std::snprintf(b, sizeof b, "%0*llx", w, static_cast<unsigned long long>(v)); return b; }
struct NoiseTpl { const char* event; const char* level; const char* msg; };
const std::vector<NoiseTpl> kNoise[NSVC] = {
    {{"tls_handshake_error", "warn", "TLS handshake error from client"}, {"request_body_too_large", "warn", "rejected request body over limit"}, {"client_disconnect", "warn", "client closed connection before response"}},
    {{"schema_coercion", "warn", "coerced legacy order field to new schema"}, {"deprecated_api_call", "warn", "client called deprecated endpoint /v1/submit"}, {"validation_hook_failed", "error", "unhandled exception in validation hook"}},
    {{"partition_rebalance", "warn", "consumer group rebalance in progress"}, {"segment_roll", "info", "rolled log segment"}, {"consumer_lag_sampled", "warn", "consumer lag above 500 messages (sampled)"}},
    {{"webhook_signature_invalid", "warn", "webhook signature verification failed"}, {"card_declined", "warn", "card declined by issuer"}, {"audit_flush_retry", "error", "audit log flush failed, will retry"}},
    {{"carrier_api_slow", "warn", "carrier rate API responded slowly (sampled)"}, {"address_normalized", "warn", "address normalized with low confidence"}, {"label_cache_miss", "warn", "shipping label cache miss"}},
    {{"template_missing_locale", "warn", "notification template missing locale, using default"}, {"push_token_expired", "warn", "push token expired for device"}, {"smtp_retry", "error", "smtp relay temporarily unavailable, will retry"}}};

class World {
 public:
  World(const Spec& spec, bool drop_true) : spec_(spec), rng_(spec.seed), logrng_(spec.seed ^ 0x5bd1e995ULL) {
    for (size_t i = 0; i < spec.faults.size(); ++i) if (!(drop_true && i == 0)) faults_.push_back(spec.faults[i]);
    for (int s = 0; s < NSVC; ++s) st_[s].offset_us = static_cast<int64_t>(rng_.uni(-3, 3) * kMs + spec.skew_s[s] * kSec);
  }

  sim::Output run() {
    for (int s = 0; s < NSVC; ++s) { st_[s].boots = 1; st_[s].boot_t = -static_cast<int64_t>(rng_.uni(600, 36000)) * kSec; }
    ops_offset_us_ = static_cast<int64_t>(rng_.uni(-150, 150) * kMs);
    schedule_faults(); schedule_noise(); schedule_load();
    for (int s = 0; s < NSVC; ++s) {
      const int64_t phase = static_cast<int64_t>(rng_.uni(0, 5) * kSec);
      for (int64_t t = 5 * kSec + phase; t <= kDurationUs; t += 5 * kSec) at(t, [this, s] { tick(s); });
    }
    while (!pq_.empty()) {
      Ev e = pq_.top(); pq_.pop();
      if (e.t > kDurationUs + 10 * kSec) break;
      now_ = e.t; e.fn();
    }
    return finish();
  }

 private:
  void at(int64_t t, std::function<void()> f) { pq_.push({t, seq_++, std::move(f)}); }
  void after(int64_t d, std::function<void()> f) { at(now_ + d, std::move(f)); }
  template <class F> std::function<void()> guard(int svc, int gen, F f) { return [this, svc, gen, f] { if (st_[svc].gen == gen) f(); }; }

  // ---------- fault queries ----------
  bool active(const Fault& f, int64_t t) const { return t >= f.t0 && t <= f.t1; }
  double lat_mult(int svc) {
    double m = 1;
    for (auto& f : faults_) {
      if (f.svc != svc) continue;
      if (f.kind == FaultKind::BadDeploy && active(f, now_) && now_ >= f.t0 + 4 * kSec) m *= 2.5;
      if (f.kind == FaultKind::CapacityReduction && active(f, now_)) m *= 8;
      if (f.kind == FaultKind::SilentRegression && active(f, now_)) m *= 1.5;
      if (f.kind == FaultKind::GradualDegradation && active(f, now_)) m *= 1 + (f.p1 - 1) * std::min(1.0, static_cast<double>(now_ - f.t0) / (30.0 * kSec));
    }
    const double since = static_cast<double>(now_ - st_[svc].boot_t) / kSec;
    if (since < 10) m *= 1 + 3 * (1 - since / 10);
    return m;
  }
  double extra_ms(int svc) {
    double e = 0;
    for (auto& f : faults_) {
      if (f.svc != svc || !active(f, now_)) continue;
      if (f.kind == FaultKind::SlowDependency && rng_.chance(f.p2)) e += rng_.uni(0.7 * f.p1, 1.3 * f.p1);
      if (f.kind == FaultKind::MildLatency) e += rng_.uni(0.7 * f.p1, 1.3 * f.p1);
    }
    for (auto& f : faults_)  // retry storm: trigger is a short burst of degradation in billing; f.svc is the amplifier (broker)
      if (f.kind == FaultKind::RetryStorm && svc == BILLING && now_ >= f.t0 && now_ <= f.t0 + 12 * kSec) e += f.p1 * rng_.lognormal(0.3);
    return e;
  }
  int maybe_fail(int svc, const Req& r, std::string& code) {
    for (auto& f : faults_) {
      if (f.svc != svc || !active(f, now_)) continue;
      if (f.kind == FaultKind::BadDeploy && now_ >= f.t0 + 4 * kSec && rng_.chance(f.p1)) { code = "internal_error"; return 500; }
      if (f.kind == FaultKind::MaskedError && r.poison) { code = "label_record_invalid"; return 500; }
      if (f.kind == FaultKind::CosmeticErrors && rng_.chance(0.03)) { code = "rate_limited"; return 429; }
      if (f.kind == FaultKind::SilentRegression && rng_.chance(f.p1)) { code = "internal_error"; return 500; }
    }
    for (auto& f : faults_)
      if (f.kind == FaultKind::RetryStorm && svc == BILLING && now_ >= f.t0 && now_ <= f.t0 + 12 * kSec && rng_.chance(f.p2)) { code = "temporarily_unavailable"; return 503; }
    if (rng_.chance(0.002)) { code = "internal_error"; return 500; }
    if (svc == API && rng_.chance(0.01)) { code = "invalid_request"; return 400; }
    return 0;
  }
  int workers(int svc) {
    int w = kWorkers[svc];
    for (auto& f : faults_) if (f.svc == svc && f.kind == FaultKind::CapacityReduction && active(f, now_)) w = static_cast<int>(f.p1);
    return w;
  }
  int pool_size(int svc) {
    int n = kPool[svc];
    for (auto& f : faults_)
      if (f.svc == svc && f.kind == FaultKind::PoolExhaustion && active(f, now_)) n = std::max(0, kPool[svc] - static_cast<int>((now_ - f.t0) / static_cast<int64_t>(f.p1 * kSec)));
    return n;
  }
  EdgeCfg edge(int caller, int callee) {
    EdgeCfg c{3000 * kMs, 0, 0};
    if (caller == INGEST && callee == BROKER) c = {2500 * kMs, 0, 0};
    if (caller == BROKER && callee == BILLING) c = {1200 * kMs, 1, 100 * kMs};
    if (caller == BROKER && callee == SHIPPING) c = {1000 * kMs, 1, 100 * kMs};
    if (caller == BROKER && callee == NOTIFIER) c = {800 * kMs, 0, 0};
    for (auto& f : faults_) {
      if (f.kind == FaultKind::ConfigChange && caller == BROKER && f.i1 == callee && now_ >= f.t0) c.timeout_us = static_cast<int64_t>(f.p1 * kMs);
      if (f.kind == FaultKind::RetryStorm && caller == BROKER && callee == BILLING && now_ >= f.t0) { c.retries = 6; c.backoff_us = 0; }
    }
    return c;
  }
  bool edge_fault(FaultKind k, int caller, int callee, double& p) const {
    for (auto& f : faults_)
      if (f.kind == k && f.svc == (k == FaultKind::DnsFailure ? caller : callee) && f.i1 == (k == FaultKind::DnsFailure ? callee : caller) && active(f, now_)) { p = f.p2; return true; }
    return false;
  }
  double subset_extra_ms(int svc, const Req& r) {
    for (auto& f : faults_) if (f.kind == FaultKind::SlowSubset && f.svc == svc && r.poison && active(f, now_)) return f.p1 * rng_.lognormal(0.25);
    return 0;
  }

  // ---------- emission ----------
  std::string pick(std::initializer_list<const char*> v) { return *(v.begin() + logrng_.range(0, static_cast<int64_t>(v.size()) - 1)); }
  void log(int svc, const char* level, const char* event, const std::string& msg, const std::string& trace, const std::string& fields = "{}", bool exempt = false) {
    if (!exempt && logrng_.chance(0.03)) return;
    auto& S = st_[svc];
    Obj o;
    o.s("id", std::string(kSvcName[svc]) + "-L" + hex(++S.log_n, 5)).i("ts_us", spec_.epoch_us + now_ + S.offset_us).s("service", kSvcName[svc]).s("level", level);
    if (event && *event && (exempt || !logrng_.chance(0.10))) o.s("event", event);
    o.s("msg", msg);
    if (!trace.empty() && (exempt || !logrng_.chance(0.05))) o.s("trace_id", trace);
    if (fields != "{}") o.raw("f", fields);
    S.logs += o.str() + "\n";
  }
  void ops(const char* type, int svc, const std::string& detail, double p = 1.0) {
    if (!logrng_.chance(p)) return;
    Obj o;
    o.s("id", "ops-E" + hex(++ops_n_, 4)).i("ts_us", spec_.epoch_us + now_ + ops_offset_us_).s("source", "ops").s("type", type).s("service", kSvcName[svc]).raw("detail", detail);
    events_ += o.str() + "\n";
  }
  void span(int svc, const std::string& trace, const std::string& sid, const std::string& parent, const char* kind, const std::string& op, const std::string& peer,
            int64_t start, int64_t dur, int status, const std::string& err, int attempt, int64_t queue_us) {
    if (logrng_.chance(0.04)) return;
    auto& S = st_[svc];
    Obj o;
    o.s("id", std::string(kSvcName[svc]) + "-S" + hex(++S.span_n, 5)).s("trace_id", trace).s("span_id", sid);
    if (!parent.empty()) o.s("parent_span_id", parent);
    o.s("service", kSvcName[svc]).s("kind", kind).s("op", op);
    if (!peer.empty()) o.s("peer", peer);
    o.i("start_us", spec_.epoch_us + start + S.offset_us).i("dur_us", dur).i("status", status);
    if (!err.empty()) o.s("error", err);
    if (attempt > 1 || std::string(kind) == "client") o.i("attempt", attempt);
    if (queue_us > 0) o.i("queue_us", queue_us);
    S.spans += o.str() + "\n";
  }
  std::string new_span() { return hex(rng_.next(), 16); }
  void record_server(int svc, int64_t dur, int status) { auto& w = st_[svc].win; ++w.n; if (status >= 500) ++w.err; w.durs.push_back(dur); }
  void sample_gauges(int svc) {
    auto& S = st_[svc];
    S.win.qmax = std::max<int>(S.win.qmax, static_cast<int>(S.q.size()));
    S.win.busymax = std::max(S.win.busymax, S.busy);
    S.win.poolmax = std::max(S.win.poolmax, S.pool.inuse + (kPool[svc] - pool_size(svc)));
  }
  void tick(int svc) {
    auto& S = st_[svc];
    sample_gauges(svc);
    if (S.up) {
      auto d = S.win.durs;
      std::sort(d.begin(), d.end());
      auto pct = [&](double p) { return d.empty() ? 0.0 : d[std::min(d.size() - 1, static_cast<size_t>(p * d.size()))] / 1000.0; };
      Obj o;
      o.s("id", std::string(kSvcName[svc]) + "-M" + hex(++mcount_[svc], 4)).i("ts_us", spec_.epoch_us + now_ + S.offset_us).s("service", kSvcName[svc]).i("window_s", 5)
          .i("req", S.win.n).i("err5xx", S.win.err).d("p50_ms", pct(0.5)).d("p99_ms", pct(0.99)).i("queue_max", S.win.qmax)
          .i("busy_max", S.win.busymax).i("workers_limit", workers(svc)).i("pool_in_use_max", S.win.poolmax).i("boots", S.boots);
      S.metrics += o.str() + "\n";
    }
    S.win = Win{};
  }

  // ---------- lifecycle ----------
  void boot(int svc, bool crashed) {
    auto& S = st_[svc];
    S.up = true; ++S.boots; S.boot_t = now_;
    log(svc, "info", "service_started", "service started version=" + S.version, "", Obj().s("version", S.version).s("boot_id", hex(rng_.next(), 8)).str(), true);
    if (crashed) ops("container_restarted", svc, Obj().s("reason", "exited").str(), 0.9);
  }
  void crash(int svc) {
    auto& S = st_[svc];
    if (rng_.chance(0.6)) log(svc, "fatal", "oom_kill", "out of memory: process killed", "", "{}", true);
    S.up = false; ++S.gen; S.busy = 0; S.q.clear(); S.pool = Pool{};
  }
  void schedule_faults() {
    for (auto& f : faults_) {
      const Fault ff = f;
      const int s = ff.svc;
      switch (ff.kind) {
        case FaultKind::BadDeploy:
        case FaultKind::HarmlessDeploy: {
          const std::string from = st_[s].version, to = "v1." + std::to_string(rng_.range(1, 9)) + "." + std::to_string(rng_.range(0, 9));
          at(ff.t0, [this, s, from, to] { ops("deploy", s, Obj().s("phase", "started").s("from", from).s("to", to).str()); });
          at(ff.t0 + 4 * kSec, [this, s, from, to] {
            st_[s].version = to;
            ops("deploy", s, Obj().s("phase", "finished").s("from", from).s("to", to).str());
            log(s, "info", "service_started", "service started version=" + to, "", Obj().s("version", to).str(), true);
          });
          if (ff.kind == FaultKind::BadDeploy && ff.i1 && ff.t1 < kDurationUs) at(ff.t1, [this, s, from] { ops("deploy", s, Obj().s("phase", "rollback").s("to", from).str(), 0.8); });
          break;
        }
        case FaultKind::ConfigChange: {
          const int callee = ff.i1;
          const std::string key = std::string("timeout.") + kSvcName[callee] + "_ms";
          const int64_t oldv = callee == BILLING ? 1200 : 1000;
          at(ff.t0, [this, s, key, oldv, ff] {
            log(s, "info", "config_changed", "config reloaded " + key, "", Obj().s("key", key).i("old", oldv).i("new", static_cast<int64_t>(ff.p1)).str(), true);
            ops("config_change", s, Obj().s("key", key).i("old", oldv).i("new", static_cast<int64_t>(ff.p1)).str(), 0.9);
          });
          break;
        }
        case FaultKind::CrashRestart:
          at(ff.t0, [this, s] { crash(s); });
          at(ff.t1, [this, s] { boot(s, true); });
          break;
        case FaultKind::CrashLoop:
          for (int k = 0; k < 3; ++k) { at(ff.t0 + k * 14 * kSec, [this, s] { crash(s); }); at(ff.t0 + k * 14 * kSec + 3 * kSec, [this, s] { boot(s, true); }); }
          break;
        case FaultKind::CapacityReduction:
          at(ff.t0, [this, s] { ops("resource_limit_change", s, Obj().s("resource", "cpu").s("action", "throttled").str(), 0.4); });
          at(ff.t1, [this, s] { try_start(s); });
          break;
        case FaultKind::SlowDependency:
          at(ff.t0, [this, s] { ops("maintenance", s, Obj().s("note", "scheduled maintenance window").str(), 0.3); });
          break;
        default: break;
      }
    }
  }
  void schedule_noise() {
    for (int s = 0; s < NSVC; ++s) {
      int64_t t = 0;
      while (true) {
        t += static_cast<int64_t>(rng_.expo(0.25) * kSec);
        if (t > kDurationUs) break;
        const auto& n = kNoise[s][rng_.range(0, 2)];
        at(t, [this, s, &n] { log(s, n.level, n.event, n.msg, ""); });
      }
    }
  }
  void schedule_load() {
    int64_t t = 500 * kMs;
    while (true) {
      t += static_cast<int64_t>(rng_.expo(kRps) * kSec);
      if (t > kDurationUs) break;
      const bool poison = rng_.chance(0.25);
      const std::string trace = hex(rng_.next(), 16) + hex(rng_.next(), 16);
      at(t, [this, poison, trace, t] {
        Req r; r.trace = trace; r.op = "POST /orders"; r.poison = poison;
        arrive(API, r, [this, t](int status, const std::string&) { gw_.push_back({t, status}); });
      });
    }
  }

  // ---------- RPC machinery ----------
  int64_t net() { return static_cast<int64_t>(rng_.uni(300, 900)); }
  struct CallCtx { int caller, callee, caller_gen; std::string op, trace, parent; bool poison; int attempt = 0; Done done; };
  void call(int caller, int callee, const std::string& op, const std::string& trace, const std::string& parent, bool poison, Done done) {
    auto c = std::make_shared<CallCtx>();
    *c = CallCtx{caller, callee, st_[caller].gen, op, trace, parent, poison, 0, std::move(done)};
    run_attempt(c);
  }
  void run_attempt(std::shared_ptr<CallCtx> c) {
    if (st_[c->caller].gen != c->caller_gen) return;
    const EdgeCfg cfg = edge(c->caller, c->callee);
    const int attempt = ++c->attempt;
    const std::string cspan = new_span();
    const int64_t t_start = now_;
    auto resolved = std::make_shared<bool>(false);
    auto finish = [this, c, cfg, attempt, cspan, t_start, resolved](int status, const std::string& err) {
      if (*resolved) return;
      *resolved = true;
      if (st_[c->caller].gen != c->caller_gen) return;
      const bool fail = status <= 0 || status >= 500;
      const std::string peer = kSvcName[c->callee];
      const std::string code = status == 0 ? "timeout" : status == -1 ? "conn_refused" : err;
      span(c->caller, c->trace, cspan, c->parent, "client", c->op, peer, t_start, now_ - t_start, status, fail ? code : "", attempt, 0);
      if (status == 0) {
        log(c->caller, "warn", "upstream_timeout", pick({"upstream call timed out", "timeout calling dependency", "deadline exceeded waiting for response"}) + (logrng_.chance(0.5) ? " peer=" + peer : ""), c->trace,
            Obj().s("peer", peer).i("timeout_ms", cfg.timeout_us / kMs).i("attempt", attempt).str());
      } else if (status == -3) {
        log(c->caller, "error", "dns_lookup_failed", "name resolution failed for " + peer, c->trace, Obj().s("peer", peer).i("attempt", attempt).str());
      } else if (status == -1) {
        log(c->caller, "error", "conn_refused", "connection refused by " + peer, c->trace, Obj().s("peer", peer).i("attempt", attempt).str());
      } else if (status >= 500) {
        const bool name_peer = logrng_.chance(0.5);
        log(c->caller, "error", "upstream_error", name_peer ? "dependency " + peer + " returned " + std::to_string(status) : "delivery step failed", c->trace,
            name_peer ? Obj().s("peer", peer).i("status", status).i("attempt", attempt).str() : Obj().i("status", status).str());
      }
      if (fail && attempt <= cfg.retries) {
        log(c->caller, "warn", "retry_scheduled", "retrying call attempt=" + std::to_string(attempt + 1), c->trace, Obj().s("peer", peer).i("next_attempt", attempt + 1).i("backoff_ms", cfg.backoff_us / kMs).str());
        after(cfg.backoff_us, [this, c] { run_attempt(c); });
      } else {
        c->done(status, code);
      }
    };
    at(now_ + cfg.timeout_us, [finish] { finish(0, "timeout"); });
    double p = 0;
    if (edge_fault(FaultKind::DnsFailure, c->caller, c->callee, p) && rng_.chance(p)) { after(2 * kMs, [finish] { finish(-3, "dns_resolution_failed"); }); return; }
    if (edge_fault(FaultKind::NetworkPartition, c->caller, c->callee, p) && rng_.chance(p)) return;
    Req r; r.trace = c->trace; r.parent_span = cspan; r.op = c->op; r.caller = c->caller; r.attempt = attempt; r.poison = c->poison;
    after(net(), [this, c, r, finish] { arrive(c->callee, r, [this, finish](int s, const std::string& e) { after(net(), [finish, s, e] { finish(s, e); }); }); });
  }

  void arrive(int svc, const Req& r, Reply reply) {
    auto& S = st_[svc];
    if (!S.up) { after(1 * kMs, [reply] { reply(-1, "conn_refused"); }); return; }
    if (S.q.size() >= kQueueCap) {
      log(svc, "warn", "load_shed", "queue full, shedding request", r.trace, Obj().i("queue", static_cast<int64_t>(S.q.size())).str());
      reply(503, "overloaded");
      return;
    }
    S.q.push_back({r, std::move(reply), now_});
    try_start(svc);
    sample_gauges(svc);
  }
  void try_start(int svc) {
    auto& S = st_[svc];
    while (S.up && S.busy < workers(svc) && !S.q.empty()) { Pending p = std::move(S.q.front()); S.q.pop_front(); ++S.busy; handle(svc, std::move(p)); }
  }
  void acquire(int svc, int gen, int64_t max_wait, std::function<void(bool)> cb) {
    auto& P = st_[svc].pool;
    if (P.inuse < pool_size(svc)) { ++P.inuse; cb(true); return; }
    auto done = std::make_shared<bool>(false);
    P.waiters.push_back({done, cb});
    at(now_ + max_wait, guard(svc, gen, [done, cb] { if (*done) return; *done = true; cb(false); }));
  }
  void release(int svc) {
    auto& P = st_[svc].pool;
    --P.inuse;
    while (!P.waiters.empty()) {
      if (*P.waiters.front().done) { P.waiters.pop_front(); continue; }
      if (P.inuse >= pool_size(svc)) break;
      auto w = P.waiters.front(); P.waiters.pop_front();
      *w.done = true; ++P.inuse; w.cb(true);
    }
  }
  static int map_status(int s) { return s == 0 ? 504 : s < 0 ? 503 : s; }

  void handle(int svc, Pending p) {
    const int gen = st_[svc].gen;
    const int64_t t_arr = p.enq, queue_us = now_ - p.enq;
    const std::string sspan = new_span();
    const Req r = p.req;
    Reply reply = p.reply;
    auto fin = [this, svc, gen, t_arr, queue_us, sspan, r, reply](int status, const std::string& err) {
      if (st_[svc].gen != gen) return;
      auto& S = st_[svc];
      --S.busy;
      const int64_t dur = now_ - t_arr;
      record_server(svc, dur, status);
      span(svc, r.trace, sspan, r.parent_span, "server", r.op, "", t_arr, dur, status, status >= 400 ? err : "", r.attempt, queue_us);
      if (status >= 500) log(svc, "error", err.c_str(), pick({"request failed: ", "error handling request: ", "failed: "}) + err, r.trace, Obj().i("status", status).str());
      else if (status >= 400 && status != 400) log(svc, "warn", err.c_str(), "request rejected: " + err, r.trace, Obj().i("status", status).str());
      else if (dur > 400 * kMs) log(svc, "warn", "slow_request", "slow request", r.trace, Obj().i("dur_ms", dur / kMs).str());
      else if (logrng_.chance(0.03)) log(svc, "info", "request_completed", "request completed", r.trace, Obj().i("dur_ms", dur / kMs).str());
      reply(status, err);
      try_start(svc);
    };
    std::string code;
    const int fail = maybe_fail(svc, r, code);
    double ms = kBaseMs[svc] * rng_.lognormal(0.35) * lat_mult(svc);
    if (fail) ms *= 0.5;
    ms += extra_ms(svc) + subset_extra_ms(svc, r);
    const int64_t d = std::max<int64_t>(200, static_cast<int64_t>(ms * kMs));

    switch (svc) {
      case API:
        after(d, guard(svc, gen, [=, this] {
          if (fail) { fin(fail, code); return; }
          call(API, INGEST, "submit_order", r.trace, sspan, r.poison, [=](int s, const std::string& e) { if (s == 200) fin(200, ""); else fin(map_status(s), s == 0 ? "upstream_timeout" : e); });
        }));
        break;
      case INGEST:
        after(d, guard(svc, gen, [=, this] {
          if (fail) { fin(fail, code); return; }
          call(INGEST, BROKER, "publish", r.trace, sspan, r.poison, [=](int s, const std::string& e) { if (s == 200) fin(200, ""); else fin(map_status(s), s == 0 ? "upstream_timeout" : e); });
        }));
        break;
      case BROKER:
        after(d, guard(svc, gen, [=, this] {
          if (fail) { fin(fail, code); return; }
          // Parallel fan-out to billing and shipping (both awaited); notifier is fire-and-forget.
          auto remaining = std::make_shared<int>(2);
          auto any_bad = std::make_shared<bool>(false);
          auto bad_status = std::make_shared<int>(0);
          auto bad_code = std::make_shared<std::string>();
          auto joined = [=](int s, const std::string& e) {
            if (s != 200 && !*any_bad) { *any_bad = true; *bad_status = s; *bad_code = e; }
            if (--*remaining == 0) {
              if (!*any_bad) fin(200, "");
              else fin(map_status(*bad_status), *bad_status == 0 ? "upstream_timeout" : *bad_status >= 500 ? "delivery_failed" : *bad_code);
            }
          };
          call(BROKER, BILLING, "charge", r.trace, sspan, false, joined);
          call(BROKER, SHIPPING, "reserve_shipment", r.trace, sspan, r.poison, joined);
          call(BROKER, NOTIFIER, "send_notification", r.trace, sspan, false, [](int, const std::string&) {});
        }));
        break;
      case BILLING:
        acquire(BILLING, gen, 300 * kMs, [=, this](bool ok) {
          if (!ok) {
            log(BILLING, "error", "pool_exhausted", pick({"db connection pool exhausted", "timed out waiting for db connection"}), r.trace, Obj().i("pool_size", pool_size(BILLING)).str());
            fin(503, "pool_exhausted");
            return;
          }
          sample_gauges(BILLING);
          after(d, guard(svc, gen, [=, this] { release(BILLING); if (fail) fin(fail, code); else fin(200, ""); }));
        });
        break;
      case SHIPPING:
      case NOTIFIER:
        after(d, guard(svc, gen, [=] { if (fail) fin(fail, code); else fin(200, ""); }));
        break;
    }
  }

  sim::Output finish() {
    sim::Output o;
    std::string services = "[";
    for (int s = 0; s < NSVC; ++s) services += std::string(s ? "," : "") + "\"" + kSvcName[s] + "\"";
    services += "]";
    o.files["meta.json"] = Obj().s("id", spec_.id).i("window_start_us", spec_.epoch_us).i("window_end_us", spec_.epoch_us + kDurationUs).raw("services", services).s("description", kDescription).str() + "\n";
    std::string logs, spans, metrics;
    for (int s = 0; s < NSVC; ++s) { logs += st_[s].logs; spans += st_[s].spans; metrics += st_[s].metrics; }
    o.files["logs.jsonl"] = logs; o.files["spans.jsonl"] = spans; o.files["metrics.jsonl"] = metrics; o.files["events.jsonl"] = events_;
    const Fault tf = spec_.faults.empty() ? Fault{FaultKind::HarmlessDeploy, 0, 0, 0} : spec_.faults[0];
    auto rate = [&](int64_t a, int64_t b) {
      int n = 0, e = 0;
      for (auto& [t, s] : gw_) if (t >= a && t < b) { ++n; if (s >= 500 || s <= 0) ++e; }
      return n ? static_cast<double>(e) / n : 0.0;
    };
    o.err_overall = rate(0, kDurationUs);
    for (int64_t a = 0; a < kDurationUs; a += 5 * kSec) o.err_series5.push_back(rate(a, a + 5 * kSec));
    for (int64_t a = 0; a + 30 * kSec <= kDurationUs; a += 5 * kSec) {
      int n = 0, e = 0;
      for (auto& [t, st] : gw_) if (t >= a && t < a + 30 * kSec) { ++n; if (st >= 500 || st <= 0) ++e; }
      if (n >= 100) o.err_peak30 = std::max(o.err_peak30, static_cast<double>(e) / n);
    }
    o.err_baseline = rate(2 * kSec, tf.t0);
    o.err_incident = rate(tf.t0 + 3 * kSec, std::min(kDurationUs, tf.t1 + 5 * kSec));
    return o;
  }

  const Spec& spec_;
  Rng rng_, logrng_;
  std::vector<Fault> faults_;
  std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> pq_;
  uint64_t seq_ = 0;
  int64_t now_ = 0;
  SvcState st_[NSVC];
  uint64_t mcount_[NSVC] = {0, 0, 0, 0, 0, 0};
  uint64_t ops_n_ = 0;
  int64_t ops_offset_us_ = 0;
  std::string events_;
  std::vector<std::pair<int64_t, int>> gw_;
};

Spec spec_from(const ReplayEnv& env, const std::vector<bool>& enabled) {
  Spec s;
  s.id = env.id; s.seed = env.seed; s.epoch_us = env.epoch_us;
  for (int i = 0; i < NSVC; ++i) s.skew_s[i] = env.skew_s[i];
  for (size_t i = 0; i < env.conditions.size(); ++i) {
    if (!enabled[i]) continue;
    const sim::Condition& c = env.conditions[i];
    s.faults.push_back(Fault{static_cast<FaultKind>(c.kind), c.svc, c.t0, c.t1, c.p1, c.p2, c.i1});
  }
  return s;
}

}  // namespace

sim::Output simulate(const Spec& spec, bool drop_true_cause) { World w(spec, drop_true_cause); return w.run(); }
double replay_error_rate(const ReplayEnv& env, const std::vector<bool>& enabled) { return simulate(spec_from(env, enabled), false).err_peak30; }
std::vector<double> replay_failure_series(const ReplayEnv& env, const std::vector<bool>& enabled) { return simulate(spec_from(env, enabled), false).err_series5; }

std::string replay_env_json(const Spec& spec) {
  std::vector<Fault> fs = spec.faults;
  Rng r(sim::fnv1a(spec.id) ^ 0x9e3779b97f4a7c15ULL);
  for (size_t i = fs.size(); i > 1; --i) std::swap(fs[i - 1], fs[r.range(0, static_cast<int64_t>(i) - 1)]);
  std::string conds = "[";
  for (size_t i = 0; i < fs.size(); ++i) {
    if (i) conds += ",";
    Obj c;
    c.s("class", sim::condition_class(static_cast<int>(fs[i].kind))).i("kind", static_cast<int>(fs[i].kind)).s("service", kSvcName[fs[i].svc]).i("svc", fs[i].svc)
        .i("t0_us", fs[i].t0).i("t1_us", fs[i].t1).d("p1", fs[i].p1).d("p2", fs[i].p2).i("i1", fs[i].i1);
    const bool edge = fs[i].kind == FaultKind::NetworkPartition || fs[i].kind == FaultKind::DnsFailure;
    if (edge) c.s("peer", kSvcName[fs[i].i1]);
    conds += c.str();
  }
  conds += "]";
  std::string skew = "[";
  for (int s = 0; s < NSVC; ++s) { if (s) skew += ","; char b[32]; std::snprintf(b, sizeof b, "%.6f", spec.skew_s[s]); skew += b; }
  skew += "]";
  return Obj().s("topology", "fanout").s("id", spec.id).i("seed", static_cast<int64_t>(spec.seed)).i("epoch_us", spec.epoch_us).raw("skew_s", skew).raw("conditions", conds).str() + "\n";
}

std::string label_json(const Spec& spec, const sim::Output& real, const sim::Output& cf) {
  const Fault& t = spec.faults[0];
  Obj root;
  root.s("type", sim::kind_name(t.kind)).s("service", kSvcName[t.svc]).i("onset_us", spec.epoch_us + t.t0).i("end_us", spec.epoch_us + t.t1);
  if (t.kind == FaultKind::ConfigChange) root.s("target_dependency", kSvcName[t.i1]);
  if (t.kind == FaultKind::RetryStorm) root.s("trigger_service", kSvcName[BILLING]);
  if (t.kind == FaultKind::NetworkPartition) root.raw("edge", Obj().s("from", kSvcName[t.i1]).s("to", kSvcName[t.svc]).str());
  std::string decoys = "[";
  for (size_t i = 1; i < spec.faults.size(); ++i) {
    if (i > 1) decoys += ",";
    decoys += Obj().s("type", sim::kind_name(spec.faults[i].kind)).s("service", kSvcName[spec.faults[i].svc]).i("onset_us", spec.epoch_us + spec.faults[i].t0).str();
  }
  decoys += "]";
  std::string comp = "[";
  bool first = true;
  for (int s = 0; s < NSVC; ++s)
    if (spec.skew_s[s] != 0) { if (!first) comp += ","; first = false; comp += Obj().s("type", "clock_skew").s("service", kSvcName[s]).i("offset_us", static_cast<int64_t>(spec.skew_s[s] * kSec)).str(); }
  comp += "]";
  return Obj().s("id", spec.id).s("group", spec.group).s("split", spec.split).s("topology", "fanout").i("seed", static_cast<int64_t>(spec.seed)).raw("root_cause", root.str())
      .raw("decoys", decoys).raw("complications", comp).s("symptom_service", "api").d("err_rate_baseline", real.err_baseline)
      .d("err_rate_incident", real.err_incident).d("err_rate_incident_without_cause", cf.err_incident).str() + "\n";
}

// ---------------- catalog (14 incidents; fixed plan, see eval/topology2/PLAN.md) ----------------
namespace {
Fault base_fault(FaultKind k, Rng& r) {
  Fault f{k, 0, r.range(45, 64) * kSec + r.range(0, 999999), 0};
  f.t1 = f.t0 + r.range(35, 55) * kSec + r.range(0, 999999);
  return f;
}
Fault make(FaultKind k, Rng& r) {
  Fault f = base_fault(k, r);
  switch (k) {
    case FaultKind::SlowDependency: f.svc = BILLING; f.p1 = r.uni(1500, 2500); f.p2 = 0.8; break;
    case FaultKind::BadDeploy: f.svc = SHIPPING; f.p1 = r.uni(0.5, 0.8); f.i1 = r.chance(0.5); if (!f.i1) f.t1 = kDurationUs; break;
    case FaultKind::CrashRestart: f.svc = BILLING; f.t1 = f.t0 + r.range(8, 16) * kSec + r.range(0, 999999); break;
    case FaultKind::PoolExhaustion: f.svc = BILLING; f.t1 = kDurationUs; f.p1 = r.uni(1.5, 3.0); break;
    case FaultKind::ConfigChange: f.svc = BROKER; f.i1 = SHIPPING; f.p1 = r.uni(4, 10); f.t1 = kDurationUs; break;
    case FaultKind::CapacityReduction: f.svc = BROKER; f.p1 = 1; break;
    case FaultKind::RetryStorm: f.svc = BROKER; f.t1 = f.t0 + 30 * kSec; f.p1 = r.uni(250, 500); f.p2 = r.uni(0.5, 0.7); break;
    case FaultKind::MaskedError: f.svc = SHIPPING; break;
    case FaultKind::NetworkPartition: f.svc = SHIPPING; f.i1 = BROKER; f.p2 = r.uni(0.85, 1.0); break;  // svc = callee, i1 = caller
    case FaultKind::SilentRegression: f.svc = INGEST; f.p1 = r.uni(0.5, 0.7); break;
    case FaultKind::CrashLoop: f.svc = SHIPPING; f.t1 = f.t0 + 3 * 14 * kSec; break;
    default: break;
  }
  return f;
}
Fault decoy_notifier_deploy(const Fault& cause, Rng& r) {
  Fault f{FaultKind::BadDeploy, NOTIFIER, std::clamp<int64_t>(cause.t0 + r.range(-20, 10) * kSec + r.range(0, 999999), 10 * kSec, 80 * kSec), 0, r.uni(0.5, 0.8), 0, 0};
  f.t1 = kDurationUs;  // notifier errors never reach users (fire-and-forget): a loud but harmless decoy
  return f;
}
}  // namespace

std::vector<Spec> build_catalog(uint64_t seed) {
  Rng r(seed);
  struct P { FaultKind k; int decoy; bool skew; int partition_peer; };  // decoy: 0 none, 1 notifier deploy, 2 harmless deploy on a random other service
  const P plan[] = {
      {FaultKind::SlowDependency, 0, false, 0}, {FaultKind::SlowDependency, 1, false, 0}, {FaultKind::BadDeploy, 0, false, 0}, {FaultKind::BadDeploy, 0, true, 0},
      {FaultKind::CrashRestart, 0, false, 0},   {FaultKind::PoolExhaustion, 0, false, 0}, {FaultKind::ConfigChange, 0, false, 0}, {FaultKind::CapacityReduction, 0, false, 0},
      {FaultKind::RetryStorm, 0, false, 0},     {FaultKind::MaskedError, 0, false, 0},    {FaultKind::NetworkPartition, 0, false, SHIPPING},
      {FaultKind::NetworkPartition, 2, false, BILLING}, {FaultKind::SilentRegression, 0, false, 0}, {FaultKind::CrashLoop, 0, false, 0},
  };
  std::vector<Spec> out;
  size_t i = 0;
  for (const P& p : plan) {
    Spec s;
    s.group = p.decoy ? "decoy" : p.skew ? "skew" : "plain";
    s.split = "test";
    Fault f = make(p.k, r);
    if (p.k == FaultKind::NetworkPartition) f.svc = p.partition_peer;
    s.faults.push_back(f);
    if (p.decoy == 1) s.faults.push_back(decoy_notifier_deploy(f, r));
    if (p.decoy == 2) {
      Fault d{FaultKind::HarmlessDeploy, 0, 0, 0};
      do { d.svc = static_cast<int>(r.range(0, NSVC - 1)); } while (d.svc == f.svc || d.svc == BROKER);
      d.t0 = std::clamp<int64_t>(f.t0 + r.range(-20, 10) * kSec + r.range(0, 999999), 10 * kSec, 80 * kSec);
      d.t1 = d.t0 + r.range(30, 60) * kSec;
      s.faults.push_back(d);
    }
    if (p.skew) {
      int a = static_cast<int>(r.range(0, NSVC - 1)), b;
      do { b = static_cast<int>(r.range(0, NSVC - 1)); } while (b == a);
      for (int x : {a, b}) s.skew_s[x] = (r.chance(0.5) ? 1 : -1) * r.uni(0.4, 3.0);
    }
    char id[16];
    std::snprintf(id, sizeof id, "fan-%02zu", ++i);
    s.id = id;
    s.seed = r.next();
    s.epoch_us = 1770000000LL * kSec + static_cast<int64_t>(i - 1) * 1000 * kSec;
    out.push_back(s);
  }
  return out;
}

}  // namespace tb::sim2
