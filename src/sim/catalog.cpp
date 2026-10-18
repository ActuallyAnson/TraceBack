#include <algorithm>

#include "sim.hpp"
#include "tb/rng.hpp"

namespace tb::sim {
namespace {

constexpr int64_t kSec = 1000000;

const FaultKind kBase[] = {FaultKind::SlowDependency, FaultKind::RetryStorm,      FaultKind::PoolExhaustion, FaultKind::BadDeploy,
                           FaultKind::ConfigChange,   FaultKind::CrashRestart,    FaultKind::CapacityReduction, FaultKind::MaskedError};

int pick(Rng& r, std::initializer_list<int> v) { return *(v.begin() + r.range(0, static_cast<int64_t>(v.size()) - 1)); }

Fault make_true(FaultKind k, Rng& r) {
  Fault f{k, 0, r.range(45, 65) * kSec, 0};
  const int64_t dur = r.range(35, 55) * kSec;
  f.t1 = f.t0 + dur;
  switch (k) {
    case FaultKind::SlowDependency:
      f.svc = pick(r, {INVENTORY, PAYMENTS});
      f.p1 = f.svc == INVENTORY ? r.uni(900, 1600) : r.uni(1500, 2500);
      f.p2 = 0.8;
      break;
    case FaultKind::RetryStorm:
      f.svc = ORDERS;  // amplifier; the trigger is a short payments burst
      f.t1 = f.t0 + 30 * kSec;  // end of the observed incident (retry policy stays in place)
      f.p1 = r.uni(250, 500);  // burst extra latency (ms)
      f.p2 = r.uni(0.5, 0.7);  // burst error probability
      break;
    case FaultKind::PoolExhaustion:
      f.svc = pick(r, {INVENTORY, ORDERS});
      f.t1 = kDurationUs;
      f.p1 = r.uni(1.5, 3.0);  // seconds per leaked connection
      break;
    case FaultKind::BadDeploy:
      f.svc = pick(r, {PAYMENTS, INVENTORY, ORDERS});
      f.p1 = r.uni(0.5, 0.8);
      f.i1 = r.chance(0.5);  // rolled back at t1
      if (!f.i1) f.t1 = kDurationUs;
      break;
    case FaultKind::ConfigChange:
      f.svc = ORDERS;
      f.i1 = pick(r, {INVENTORY, PAYMENTS});
      f.p1 = r.uni(4, 10);  // new timeout in ms
      f.t1 = kDurationUs;
      break;
    case FaultKind::CrashRestart:
      f.svc = pick(r, {INVENTORY, PAYMENTS, ORDERS});
      f.t1 = f.t0 + r.range(8, 16) * kSec;
      break;
    case FaultKind::CapacityReduction:
      f.svc = pick(r, {ORDERS, PAYMENTS, INVENTORY});
      f.p1 = 1;
      break;
    case FaultKind::MaskedError:
      f.svc = INVENTORY;
      break;
    default: break;
  }
  return f;
}

Fault make_decoy(const Fault& cause, Rng& r) {
  static const FaultKind d[] = {FaultKind::HarmlessDeploy, FaultKind::MildLatency, FaultKind::CosmeticErrors};
  Fault f{d[r.range(0, 2)], 0, 0, 0};
  do { f.svc = static_cast<int>(r.range(0, NSVC - 1)); } while (f.svc == cause.svc);
  f.t0 = std::clamp<int64_t>(cause.t0 + r.range(-25, 15) * kSec, 10 * kSec, 80 * kSec);
  f.t1 = f.t0 + r.range(30, 60) * kSec;
  f.p1 = r.uni(15, 40);
  return f;
}

void add_skew(Spec& s, Rng& r) {
  s.has_skew = true;
  int a = static_cast<int>(r.range(0, NSVC - 1)), b;
  do { b = static_cast<int>(r.range(0, NSVC - 1)); } while (b == a);
  for (int x : {a, b}) s.skew_s[x] = (r.chance(0.5) ? 1 : -1) * r.uni(0.4, 3.0);
}

}  // namespace

// 8 fault types x 4 plain + 8 with clock skew + 8 with a decoy fault = 48 incidents.
// Dev/test is fixed by construction here and frozen in eval/split.json.
std::vector<Spec> build_catalog(uint64_t master_seed) {
  Rng r(master_seed);
  std::vector<Spec> out;
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 4; ++i) {
      Spec s;
      s.group = "base";
      s.split = i == 3 ? "test" : "dev";
      s.faults.push_back(make_true(kBase[k], r));
      out.push_back(s);
    }
  for (int k = 0; k < 8; ++k) {
    Spec s;
    s.group = "skew";
    s.split = k % 2 ? "test" : "dev";
    s.faults.push_back(make_true(kBase[k], r));
    add_skew(s, r);
    out.push_back(s);
  }
  for (int k = 0; k < 8; ++k) {
    Spec s;
    s.group = "double";
    s.split = k % 2 ? "dev" : "test";  // opposite parity to skew: every fault type gets 2 test incidents
    s.faults.push_back(make_true(kBase[k], r));
    s.faults.push_back(make_decoy(s.faults[0], r));
    if (r.chance(0.25)) s.faults.push_back(make_decoy(s.faults[0], r));
    out.push_back(s);
  }
  // Opaque ids: shuffle so id order carries no information about type or split.
  for (size_t i = out.size() - 1; i > 0; --i) std::swap(out[i], out[r.range(0, static_cast<int64_t>(i))]);
  for (size_t i = 0; i < out.size(); ++i) {
    char id[16];
    std::snprintf(id, sizeof id, "inc-%03zu", i + 1);
    out[i].id = id;
    out[i].seed = r.next();
    out[i].epoch_us = 1760000000LL * kSec + static_cast<int64_t>(i) * 1000 * kSec;
  }
  return out;
}

}  // namespace tb::sim
