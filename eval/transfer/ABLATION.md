# Ablation of hybrid 2 (Super model)

RCAEval RE3 Sock Shop + Train Ticket (60 faults, 18 clean)
| Variant | Faults localized | Clean-window alarms | Wrong at high | Wrong at medium or high |
|---|---:|---:|---:|---:|
| A baseline | 14/60 | 16/18 | 19 | 46 |
| B + confidence cap only | 14/60 | 0/18 | 3 | 7 |
| C + drop unsupported proposals | 17/60 | 1/18 | 0 | 14 |
| D + drop symptoms | 17/60 | 1/18 | 0 | 12 |
| E hybrid 2 (frozen) | 22/60 | 8/18 | 1 | 36 |

RCAEval RE2 Sock Shop resource faults (90 faults, 30 clean)
| Variant | Faults localized | Clean-window alarms | Wrong at high | Wrong at medium or high |
|---|---:|---:|---:|---:|
| A baseline | 2/90 | 27/30 | 45 | 87 |
| B + confidence cap only | 2/90 | 9/30 | 21 | 36 |
| C + drop unsupported proposals | 2/90 | 11/30 | 0 | 44 |
| D + drop symptoms | 2/90 | 11/30 | 0 | 44 |
| E hybrid 2 (frozen) | 5/90 | 19/30 | 20 | 61 |

# Ablation of hybrid 2 (Ultra model)

RCAEval RE3 Sock Shop + Train Ticket (60 faults, 18 clean)
| Variant | Faults localized | Clean-window alarms | Wrong at high | Wrong at medium or high |
|---|---:|---:|---:|---:|
| A baseline | 19/60 | 15/18 | 8 | 39 |
| B + confidence cap only | 19/60 | 4/18 | 1 | 13 |
| C + drop unsupported proposals | 33/60 | 5/18 | 0 | 16 |
| D + drop symptoms | 36/60 | 5/18 | 0 | 8 |
| E hybrid 2 (frozen) | 38/60 | 8/18 | 0 | 19 |
