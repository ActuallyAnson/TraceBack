# What changed in the converter after seeing real data (written before any system ran)

1. Client-span peer was first matched by substring in address attributes; `valkey-cart` (the cart's store, not an instrumented service) matched `cart` and produced a bogus `cart → cart` edge on a warm-up window. Fixed to a whole-token match and never a self-call. Client spans to uninstrumented stores (postgres, valkey) therefore carry no `peer`.
Found on warm-up traffic captured before the scenario runs, not on a scored run.
