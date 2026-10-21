# Combination test (rule C4): result

Offline, $0, applied once to the 60 faults and 18 clean windows of RE3-SS and RE3-TT (plan: `PLAN.md`; frozen `LOCK`). C4 is the pre-registered rule; the other rows are context.

```

RE2-SS (development)
| Variant | Faults localized | Clean alarms | Wrong at high | Wrong at medium or high | No answer |
|---|---:|---:|---:|---:|---:|
| H | 5/90 | 19/30 | 20 | 61 | 34 |
| M | 66/90 | 4/30 | 0 | 17 | 33 |
| C2 | 22/90 | 19/30 | 1 | 67 | 11 |
| C3 | 66/90 | 20/30 | 1 | 23 | 11 |
| C4 | 67/90 | 20/30 | 1 | 22 | 11 |

RE3-SS+TT, Super
| Variant | Faults localized | Clean alarms | Wrong at high | Wrong at medium or high | No answer |
|---|---:|---:|---:|---:|---:|
| H | 22/60 | 8/18 | 1 | 36 | 12 |
| M | 11/60 | 11/18 | 0 | 37 | 19 |
| C2 | 24/60 | 15/18 | 1 | 36 | 3 |
| C3 | 17/60 | 15/18 | 1 | 43 | 3 |
| C4 | 18/60 | 15/18 | 1 | 42 | 3 |

RE3-SS+TT, Ultra
| Variant | Faults localized | Clean alarms | Wrong at high | Wrong at medium or high | No answer |
|---|---:|---:|---:|---:|---:|
| H | 38/60 | 8/18 | 0 | 19 | 12 |
| M | 11/60 | 11/18 | 0 | 37 | 19 |
| C2 | 40/60 | 15/18 | 2 | 19 | 3 |
| C3 | 19/60 | 15/18 | 2 | 41 | 3 |
| C4 | 25/60 | 15/18 | 2 | 35 | 3 |
```

## Reading (negative result, reported as measured)
- **C4 does not help on code-level faults.** With the Super model it localizes 18/60 against hybrid 2's 22/60; with Ultra 25/60 against 38/60. Clean-window alarms rise from 8/18 to 15/18, and wrong at medium or high from 36 to 42 (Super) and from 19 to 35 (Ultra). Rule M alone finds 11/60 here: these faults mostly leave the resource metrics flat, and where they do not, the loudest metric service is often a symptom.
- **The two methods want opposite priorities.** Putting the model first (C2) is better on RE3 (24/60 Super, 40/60 Ultra) but gives up the resource-fault gain on RE2-SS (22/90 against rule M's 66/90). Putting the metrics first (C3, C4) does the reverse. Nothing here chooses between them without knowing the fault class in advance, and I did not tune a router on the test windows.
- **What this means for the claims:** keep the two results separate. Hybrid 2 is the calibrated method for code-level faults visible in logs; rule M is a no-model detector for resource faults, with no calibration. The combination is not an improvement as specified, and no combined number belongs in the headline.
- Limits: the combination was designed on RE2-SS only; the model answers on RE3 were already opened for earlier tests; repeats of one fault are not independent; no significance claims.
