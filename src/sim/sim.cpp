#include "sim.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <queue>

#include "tb/jsonw.hpp"
#include "tb/rng.hpp"

namespace tb::sim {

const char* const kSvcName[NSVC] = {"gateway", "orders", "inventory", "payments"};

const char* kind_name(FaultKind k) {
  switch (k) {
    case FaultKind::SlowDependency: return "slow_dependency";
    case FaultKind::RetryStorm: return "retry_storm";
    case FaultKind::PoolExhaustion: return "connection_pool_exhaustion";
    case FaultKind::BadDeploy: return "bad_deploy";
    case FaultKind::ConfigChange: return "config_change";
    case FaultKind::CrashRestart: return "crash_restart";
    case FaultKind::CapacityReduction: return "capacity_reduction";
    case FaultKind::MaskedError: return "masked_downstream_error";
    case FaultKind::HarmlessDeploy: return "harmless_deploy";
    case FaultKind::MildLatency: return "mild_latency";
    case FaultKind::CosmeticErrors: return "cosmetic_errors";
  }
  return "?";
}

uint64_t fnv1a(const std::string& s, uint64_t h) {
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

uint64_t content_hash(const Output& o) {
  uint64_t h = 1469598103934665603ULL;
  for (const auto& [name, body] : o.files) {
    h = fnv1a(name, h);
    h = fnv1a(body, h);
  }
  return h;
}

namespace {

constexpr int64_t kMs = 1000, kSec = 1000000;
constexpr double kRps = 20;
constexpr size_t kQueueCap = 150;
const double kBaseMs[NSVC] = {2, 8, 12, 25};
const int kWorkers[NSVC] = {64, 16, 12, 12};
const int kPool[NSVC] = {0, 16, 8, 0};

struct EdgeCfg {
  int64_t timeout_us;
  int retries;
  int64_t backoff_us;
};

struct Req {
  std::string trace, parent_span, op;
  int caller = -1;
  int attempt = 1;
  bool poison = false;
};
using Reply = std::function<void(int, const std::string&)>;
using Done = std::function<void(int, const std::string&)>;

struct Pool {
  struct W {
    std::shared_ptr<bool> done;
    std::function<void(bool)> cb;
  };
  int inuse = 0;
  std::deque<W> waiters;
};

struct Pending {
  Req req;
  Reply reply;
  int64_t enq;
};

struct Win {
  int n = 0, err = 0, qmax = 0, busymax = 0, poolmax = 0;
  std::vector<int64_t> durs;
};

struct SvcState {
  bool up = true;
  int gen = 0;
  int busy = 0;
  int boots = 0;
  int64_t boot_t = -1000 * kSec;
  std::string version = "v1.0.0";
  std::deque<Pending> q;
  Pool pool;
  Win win;
  int64_t offset_us = 0;
  uint64_t log_n = 0, span_n = 0;
  std::string logs, spans, metrics;
};

struct Ev {
  int64_t t;
  uint64_t seq;
  std::function<void()> fn;
  bool operator>(const Ev& o) const { return t != o.t ? t > o.t : seq > o.seq; }
};

std::string hex(uint64_t v, int w) {
  char b[32];
  std::snprintf(b, sizeof b, "%0*llx", w, static_cast<unsigned long long>(v));
  return b;
}

struct NoiseTpl {
  const char* event;
  const char* level;
  const char* msg;
};

const std::vector<NoiseTpl> kNoise[NSVC] = {
    {{"tls_handshake_error", "warn", "TLS handshake error from client"},
     {"request_body_too_large", "warn", "rejected request body over limit"},
     {"client_disconnect", "warn", "client closed connection before response"}},
    {{"event_bus_reconnect", "warn", "event bus connection reset, reconnecting"},
     {"deprecated_api_call", "warn", "client called deprecated endpoint /v1/order"},
     {"analytics_hook_failed", "error", "unhandled exception in analytics hook"}},
    {{"slow_query_sampled", "warn", "query exceeded 100ms threshold (sampled)"},
     {"stale_cache_entry", "warn", "served stale cache entry for sku"},
     {"replica_lag", "warn", "read replica lag above 200ms"}},
    {{"webhook_signature_invalid", "warn", "webhook signature verification failed"},
     {"card_declined", "warn", "card declined by issuer"},
     {"audit_flush_retry", "error", "audit log flush failed, will retry"}}};

class World {
 public:
  World(const Spec& spec, bool drop_true)
      : spec_(spec), rng_(spec.seed), logrng_(spec.seed ^ 0x5bd1e995ULL) {
    for (size_t i = 0; i < spec.faults.size(); ++i)
      if (!(drop_true && i == 0)) faults_.push_back(spec.faults[i]);
    for (int s = 0; s < NSVC; ++s) {
      st_[s].offset_us = static_cast<int64_t>((rng_.uni(-3, 3)) * kMs + spec.skew_s[s] * kSec);
      st_[s].win = Win{};
    }
  }

  Output run() {
    for (int s = 0; s < NSVC; ++s) boot(s, false);
    schedule_faults();
    schedule_noise();
    schedule_load();
    for (int s = 0; s < NSVC; ++s)
      for (int64_t t = 5 * kSec; t <= kDurationUs; t += 5 * kSec) at(t, [this, s] { tick(s); });
    while (!pq_.empty()) {
      Ev e = pq_.top();
      pq_.pop();
      if (e.t > kDurationUs + 10 * kSec) break;
      now_ = e.t;
      e.fn();
    }
    return finish();
  }

 private:
  // ---------- event loop ----------
  void at(int64_t t, std::function<void()> f) { pq_.push({t, seq_++, std::move(f)}); }
  void after(int64_t d, std::function<void()> f) { at(now_ + d, std::move(f)); }
  template <class F>
  std::function<void()> guard(int svc, int gen, F f) {
    return [this, svc, gen, f] {
      if (st_[svc].gen == gen) f();
    };
  }

  // ---------- fault queries (injection side) ----------
  bool active(const Fault& f, int64_t t) const { return t >= f.t0 && t <= f.t1; }

  double lat_mult(int svc) {
    double m = 1;
    for (auto& f : faults_) {
      if (f.svc != svc) continue;
      if (f.kind == FaultKind::BadDeploy && active(f, now_) && now_ >= f.t0 + 4 * kSec) m *= 2.5;
      if (f.kind == FaultKind::CapacityReduction && active(f, now_)) m *= 8;
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
      if (f.kind == FaultKind::RetryStorm && now_ <= f.t0 + 12 * kSec) e += f.p1 * rng_.lognormal(0.3);
    }
    return e;
  }
  // Returns status (0 = no failure) and sets code.
  int maybe_fail(int svc, const Req& r, std::string& code) {
    for (auto& f : faults_) {
      if (f.svc != svc || !active(f, now_)) continue;
      if (f.kind == FaultKind::BadDeploy && now_ >= f.t0 + 4 * kSec && rng_.chance(f.p1)) { code = "internal_error"; return 500; }
      if (f.kind == FaultKind::RetryStorm && now_ <= f.t0 + 12 * kSec && rng_.chance(f.p2)) { code = "temporarily_unavailable"; return 503; }
      if (f.kind == FaultKind::MaskedError && r.poison) { code = "stock_record_corrupt"; return 500; }
      if (f.kind == FaultKind::CosmeticErrors && rng_.chance(0.03)) { code = "rate_limited"; return 429; }
    }
    if (rng_.chance(0.002)) { code = "internal_error"; return 500; }
    if (svc == GATEWAY && rng_.chance(0.01)) { code = "invalid_request"; return 400; }
    return 0;
  }
  int workers(int svc) {
    int w = kWorkers[svc];
    for (auto& f : faults_)
      if (f.svc == svc && f.kind == FaultKind::CapacityReduction && active(f, now_)) w = static_cast<int>(f.p1);
    return w;
  }
  int pool_size(int svc) {
    int n = kPool[svc];
    for (auto& f : faults_)
      if (f.svc == svc && f.kind == FaultKind::PoolExhaustion && active(f, now_)) {
        n = std::max(0, kPool[svc] - static_cast<int>((now_ - f.t0) / static_cast<int64_t>(f.p1 * kSec)));
      }
    return n;
  }
  EdgeCfg edge(int caller, int callee) {
    EdgeCfg c{2500 * kMs, 0, 0};
    if (caller == ORDERS && callee == INVENTORY) c = {800 * kMs, 1, 50 * kMs};
    if (caller == ORDERS && callee == PAYMENTS) c = {1200 * kMs, 2, 100 * kMs};
    for (auto& f : faults_) {
      if (f.kind == FaultKind::ConfigChange && caller == ORDERS && f.i1 == callee && now_ >= f.t0)
        c.timeout_us = static_cast<int64_t>(f.p1 * kMs);
      if (f.kind == FaultKind::RetryStorm && caller == ORDERS && callee == PAYMENTS && now_ >= f.t0) {
        c.retries = 6;
        c.backoff_us = 0;
      }
    }
    return c;
  }

  // ---------- emission ----------
  std::string pick(std::initializer_list<const char*> v) {
    return *(v.begin() + logrng_.range(0, static_cast<int64_t>(v.size()) - 1));
  }
  void log(int svc, const char* level, const char* event, const std::string& msg, const std::string& trace,
           const std::string& fields = "{}", bool exempt = false) {
    if (!exempt && logrng_.chance(0.03)) return;  // lost line
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
    o.s("id", "ops-E" + hex(++ops_n_, 4)).i("ts_us", spec_.epoch_us + now_).s("source", "ops").s("type", type).s("service", kSvcName[svc]).raw("detail", detail);
    events_ += o.str() + "\n";
  }
  void span(int svc, const std::string& trace, const std::string& sid, const std::string& parent, const char* kind,
            const std::string& op, const std::string& peer, int64_t start, int64_t dur, int status, const std::string& err,
            int attempt, int64_t queue_us) {
    if (logrng_.chance(0.04)) return;  // lost span
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
  void record_server(int svc, int64_t dur, int status) {
    auto& w = st_[svc].win;
    ++w.n;
    if (status >= 500) ++w.err;
    w.durs.push_back(dur);
  }
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
    S.up = true;
    ++S.boots;
    S.boot_t = now_;
    log(svc, "info", "service_started", "service started version=" + S.version, "", Obj().s("version", S.version).s("boot_id", hex(rng_.next(), 8)).str(), true);
    if (crashed) ops("container_restarted", svc, Obj().s("reason", "exited").str(), 0.9);
  }
  void crash(int svc) {
    auto& S = st_[svc];
    if (rng_.chance(0.6)) log(svc, "fatal", "oom_kill", "out of memory: process killed", "", "{}", true);
    S.up = false;
    ++S.gen;
    S.busy = 0;
    S.q.clear();
    S.pool = Pool{};
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
          if (ff.kind == FaultKind::BadDeploy && ff.i1 && ff.t1 < kDurationUs)
            at(ff.t1, [this, s, from] { ops("deploy", s, Obj().s("phase", "rollback").s("to", from).str(), 0.8); });
          break;
        }
        case FaultKind::ConfigChange: {
          const int callee = ff.i1;
          const std::string key = std::string("timeout.") + kSvcName[callee] + "_ms";
          const int64_t oldv = callee == INVENTORY ? 800 : 1200;
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
        Req r;
        r.trace = trace;
        r.op = "POST /checkout";
        r.poison = poison;
        arrive(GATEWAY, r, [this, t](int status, const std::string&) { gw_.push_back({t, status}); });
      });
    }
  }

  // ---------- RPC machinery ----------
  int64_t net() { return static_cast<int64_t>(rng_.uni(300, 900)); }

  struct CallCtx {
    int caller, callee, caller_gen;
    std::string op, trace, parent;
    bool poison;
    int attempt = 0;
    Done done;
  };
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
      } else if (status == -1) {
        log(c->caller, "error", "conn_refused", "connection refused by " + peer, c->trace, Obj().s("peer", peer).i("attempt", attempt).str());
      } else if (status >= 500) {
        const bool name_peer = logrng_.chance(0.5);
        log(c->caller, "error", "upstream_error", name_peer ? "dependency " + peer + " returned " + std::to_string(status) : "order step failed", c->trace,
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
    Req r;
    r.trace = c->trace;
    r.parent_span = cspan;
    r.op = c->op;
    r.caller = c->caller;
    r.attempt = attempt;
    r.poison = c->poison;
    after(net(), [this, c, r, finish] {
      arrive(c->callee, r, [this, finish](int s, const std::string& e) { after(net(), [finish, s, e] { finish(s, e); }); });
    });
  }

  void arrive(int svc, const Req& r, Reply reply) {
    auto& S = st_[svc];
    if (!S.up) {
      after(1 * kMs, [reply] { reply(-1, "conn_refused"); });
      return;
    }
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
    while (S.up && S.busy < workers(svc) && !S.q.empty()) {
      Pending p = std::move(S.q.front());
      S.q.pop_front();
      ++S.busy;
      handle(svc, std::move(p));
    }
  }

  void acquire(int svc, int gen, int64_t max_wait, std::function<void(bool)> cb) {
    auto& P = st_[svc].pool;
    if (P.inuse < pool_size(svc)) {
      ++P.inuse;
      cb(true);
      return;
    }
    auto done = std::make_shared<bool>(false);
    P.waiters.push_back({done, cb});
    at(now_ + max_wait, guard(svc, gen, [done, cb] {
         if (*done) return;
         *done = true;
         cb(false);
       }));
  }
  void release(int svc) {
    auto& P = st_[svc].pool;
    --P.inuse;
    while (!P.waiters.empty()) {
      if (*P.waiters.front().done) { P.waiters.pop_front(); continue; }
      if (P.inuse >= pool_size(svc)) break;
      auto w = P.waiters.front();
      P.waiters.pop_front();
      *w.done = true;
      ++P.inuse;
      w.cb(true);
    }
  }

  static int map_status(int s) { return s == 0 ? 504 : s == -1 ? 503 : s; }

  void handle(int svc, Pending p) {
    const int gen = st_[svc].gen;
    const int64_t t_arr = p.enq, t_start = now_, queue_us = now_ - p.enq;
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
    (void)t_start;
    std::string code;
    const int fail = maybe_fail(svc, r, code);
    double ms = kBaseMs[svc] * rng_.lognormal(0.35) * lat_mult(svc);
    if (fail) ms *= 0.5;
    ms += extra_ms(svc);
    const int64_t d = std::max<int64_t>(200, static_cast<int64_t>(ms * kMs));

    switch (svc) {
      case GATEWAY:
        after(d, guard(svc, gen, [=, this] {
          if (fail) { fin(fail, code); return; }
          call(GATEWAY, ORDERS, "create_order", r.trace, sspan, r.poison, [=, this](int s, const std::string& e) {
            if (s == 200) fin(200, ""); else fin(map_status(s), s == 0 ? "upstream_timeout" : e);
          });
        }));
        break;
      case ORDERS:
        after(d / 2, guard(svc, gen, [=, this] {
          if (fail) { fin(fail, code); return; }
          acquire(ORDERS, gen, 300 * kMs, [=, this](bool ok) {
            if (!ok) {
              log(ORDERS, "error", "pool_exhausted", pick({"db connection pool exhausted", "timed out waiting for db connection"}), r.trace, Obj().i("pool_size", pool_size(ORDERS)).str());
              fin(503, "pool_exhausted");
              return;
            }
            sample_gauges(ORDERS);
            call(ORDERS, INVENTORY, "reserve", r.trace, sspan, r.poison, [=, this](int s, const std::string& e) {
              if (s != 200) { release(ORDERS); fin(map_status(s), s == 0 ? "upstream_timeout" : s >= 500 ? "order_failed" : e); return; }
              call(ORDERS, PAYMENTS, "charge", r.trace, sspan, false, [=, this](int s2, const std::string& e2) {
                release(ORDERS);
                if (s2 == 200) fin(200, ""); else fin(map_status(s2), s2 == 0 ? "upstream_timeout" : s2 >= 500 ? "order_failed" : e2);
              });
            });
          });
        }));
        break;
      case INVENTORY:
        acquire(INVENTORY, gen, 300 * kMs, [=, this](bool ok) {
          if (!ok) {
            log(INVENTORY, "error", "pool_exhausted", pick({"db connection pool exhausted", "timed out waiting for db connection"}), r.trace, Obj().i("pool_size", pool_size(INVENTORY)).str());
            fin(503, "pool_exhausted");
            return;
          }
          sample_gauges(INVENTORY);
          after(d, guard(svc, gen, [=, this] {
            release(INVENTORY);
            if (fail) fin(fail, code); else fin(200, "");
          }));
        });
        break;
      case PAYMENTS:
        after(d, guard(svc, gen, [=] { if (fail) fin(fail, code); else fin(200, ""); }));
        break;
    }
  }

  Output finish() {
    Output o;
    o.files["meta.json"] = Obj().s("id", spec_.id).i("window_start_us", spec_.epoch_us).i("window_end_us", spec_.epoch_us + kDurationUs)
                               .raw("services", "[\"gateway\",\"orders\",\"inventory\",\"payments\"]").str() + "\n";
    std::string logs, spans, metrics;
    for (int s = 0; s < NSVC; ++s) { logs += st_[s].logs; spans += st_[s].spans; metrics += st_[s].metrics; }
    o.files["logs.jsonl"] = logs;
    o.files["spans.jsonl"] = spans;
    o.files["metrics.jsonl"] = metrics;
    o.files["events.jsonl"] = events_;
    // gateway error rates around the true cause window
    const Fault& tf = spec_.faults[0];
    auto rate = [&](int64_t a, int64_t b) {
      int n = 0, e = 0;
      for (auto& [t, s] : gw_) if (t >= a && t < b) { ++n; if (s >= 500 || s <= 0) ++e; }
      return n ? static_cast<double>(e) / n : 0.0;
    };
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
  uint64_t mcount_[NSVC] = {0, 0, 0, 0};
  uint64_t ops_n_ = 0;
  std::string events_;
  std::vector<std::pair<int64_t, int>> gw_;
};

}  // namespace

Output simulate(const Spec& spec, bool drop_true_cause) {
  World w(spec, drop_true_cause);
  return w.run();
}

std::string label_json(const Spec& spec, const Output& real, const Output& cf) {
  const Fault& t = spec.faults[0];
  Obj root;
  root.s("type", kind_name(t.kind)).s("service", kSvcName[t.svc]).i("onset_us", spec.epoch_us + t.t0).i("end_us", spec.epoch_us + t.t1);
  if (t.kind == FaultKind::ConfigChange) root.s("target_dependency", kSvcName[t.i1]);
  if (t.kind == FaultKind::RetryStorm) root.s("trigger_service", kSvcName[PAYMENTS]);
  std::string decoys = "[";
  for (size_t i = 1; i < spec.faults.size(); ++i) {
    if (i > 1) decoys += ",";
    decoys += Obj().s("type", kind_name(spec.faults[i].kind)).s("service", kSvcName[spec.faults[i].svc]).i("onset_us", spec.epoch_us + spec.faults[i].t0).str();
  }
  decoys += "]";
  std::string comp = "[";
  bool first = true;
  for (int s = 0; s < NSVC; ++s)
    if (spec.skew_s[s] != 0) {
      if (!first) comp += ",";
      first = false;
      comp += Obj().s("type", "clock_skew").s("service", kSvcName[s]).i("offset_us", static_cast<int64_t>(spec.skew_s[s] * kSec)).str();
    }
  comp += "]";
  return Obj().s("id", spec.id).s("group", spec.group).s("split", spec.split).i("seed", static_cast<int64_t>(spec.seed)).raw("root_cause", root.str())
      .raw("decoys", decoys).raw("complications", comp).s("symptom_service", "gateway").d("err_rate_baseline", real.err_baseline)
      .d("err_rate_incident", real.err_incident).d("err_rate_incident_without_cause", cf.err_incident).str() + "\n";
}

}  // namespace tb::sim
