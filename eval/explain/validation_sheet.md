# Blind sheet: label each claim supported / partial / unsupported by the quoted records only

## Q1
STATEMENT: Broker's worker limit was reduced to 1 at +62.910s.
RECORDS:
- {"id":"broker-M000d","ts_us":1790000062909835,"service":"broker","window_s":5,"req":91,"err5xx":0,"p50_ms":218.730,"p99_ms":544.973,"queue_max":10,"busy_max":1,"workers_limit":1,"pool_in_use_max":0,"boots":1}

## Q2
STATEMENT: broker lost serving capacity; requests queue behind too few workers
RECORDS:
- {"id":"broker-M000c","ts_us":1790000057909835,"service":"broker","window_s":5,"req":89,"err5xx":0,"p50_ms":24.073,"p99_ms":114.312,"queue_max":3,"busy_max":4,"workers_limit":1,"pool_in_use_max":0,"boots":1}

## Q3
STATEMENT: broker lost serving capacity; requests queue behind too few workers
RECORDS:
- {"id":"broker-M000d","ts_us":1790015067187258,"service":"broker","window_s":5,"req":92,"err5xx":0,"p50_ms":289.454,"p99_ms":671.993,"queue_max":15,"busy_max":3,"workers_limit":1,"pool_in_use_max":0,"boots":1}

## Q4
STATEMENT: broker lost serving capacity; requests queue behind too few workers
RECORDS:
- {"id":"broker-M0009","ts_us":1790017049943078,"service":"broker","window_s":5,"req":86,"err5xx":0,"p50_ms":28.284,"p99_ms":388.843,"queue_max":8,"busy_max":4,"workers_limit":1,"pool_in_use_max":0,"boots":1}

## Q5
STATEMENT: Api calls to ingest started failing, producing upstream_error "dependency ingest returned 500".
RECORDS:
- {"id":"api-L0003d","ts_us":1790012054136233,"service":"api","level":"error","msg":"dependency ingest returned 500","trace_id":"ecb3969980a4ee4eea25fd7c788be048","f":{"peer":"ingest","status":500,"attempt":1}}

## Q6
STATEMENT: Api received errors from ingest and returned delivery_failed to users, logging upstream_error "delivery step failed" and delivery_failed.
RECORDS:
- {"id":"api-L00041","ts_us":1790007053152974,"service":"api","level":"error","event":"upstream_error","msg":"delivery step failed","trace_id":"dcc36a4c12e4067334f45f9e9c13abb5","f":{"status":500}}
- {"id":"api-L00042","ts_us":1790007053152974,"service":"api","level":"error","event":"delivery_failed","msg":"failed: delivery_failed","trace_id":"dcc36a4c12e4067334f45f9e9c13abb5","f":{"status":500}}

## Q7
STATEMENT: Broker retried the failed calls with short backoff (~100 ms).
RECORDS:
- {"id":"broker-L0006b","ts_us":1790001052658853,"service":"broker","level":"warn","event":"retry_scheduled","msg":"retrying call attempt=2","f":{"peer":"shipping","next_attempt":2,"backoff_ms":100}}

## Q8
STATEMENT: Ingest receives 504 from broker and logs upstream_error delivery step failed with status 504.
RECORDS:
- {"id":"ingest-L00042","ts_us":1790019061273360,"service":"ingest","level":"error","event":"upstream_error","msg":"delivery step failed","trace_id":"7cb987d36216c589163fdd3a7e8e2395","f":{"status":504}}

## Q9
STATEMENT: Broker retried billing calls repeatedly due to failures and latency, causing broker worker saturation and increased latency.
RECORDS:
- {"id":"broker-L00042","ts_us":1790008060676578,"service":"broker","level":"warn","event":"retry_scheduled","msg":"retrying call attempt=2","trace_id":"aedfb3f73d4785992b0c238679cda36d","f":{"peer":"billing","next_attempt":2,"backoff_ms":0}}

## Q10
STATEMENT: Payments worker limit dropped from 12 to 1, reducing its serving capacity
RECORDS:
- {"id":"payments-M000b","ts_us":1780009055455989,"service":"payments","window_s":5,"req":89,"err5xx":0,"p50_ms":25.599,"p99_ms":1145.260,"queue_max":15,"busy_max":4,"workers_limit":1,"pool_in_use_max":0,"boots":1}

## Q11
STATEMENT: Orders received connection refused when trying to call inventory.
RECORDS:
- {"id":"orders-S00e07","trace_id":"536afeaf4d777fc4043d97ca59d7aee1","span_id":"683b2e63a4b34b45","parent_span_id":"340ea3b192730c00","service":"orders","kind":"client","op":"reserve","peer":"inventory","start_us":1780020063264464,"dur_us":2203,"status":-1,"error":"conn_refused","attempt":1}
- {"id":"orders-S00e08","trace_id":"5afa8f70c7b0452b35013ca6f9ae6f38","span_id":"b4f81be686bc8d20","parent_span_id":"9459e4a7f10ac8b3","service":"orders","kind":"client","op":"reserve","peer":"inventory","start_us":1780020063302917,"dur_us":2072,"status":-1,"error":"conn_refused","attempt":1}
- {"id":"orders-S00e09","trace_id":"44d1424a36ff872329969b6b745facb0","span_id":"977d068466bf0518","parent_span_id":"8e5effba67995f7f","service":"orders","kind":"client","op":"reserve","peer":"inventory","start_us":1780020063311319,"dur_us":1757,"status":-1,"error":"conn_refused","attempt":1}

## Q12
STATEMENT: Api timeout errors were returned to users as 5xx errors.
RECORDS:
- {"id":"api-L00196","ts_us":1790015083845953,"service":"api","level":"error","event":"upstream_timeout","msg":"request failed: upstream_timeout","trace_id":"f5d2dc3f3534e79168b20f50732ea63c","f":{"status":504}}
- {"id":"api-M0011","ts_us":1790015088719290,"service":"api","window_s":5,"req":105,"err5xx":103,"p50_ms":3001.983,"p99_ms":3093.773,"queue_max":12,"busy_max":64,"workers_limit":64,"pool_in_use_max":0,"boots":1}

## Q13
STATEMENT: Gateway received upstream errors from orders (dependency orders returned 500) and returned order_failed to users.
RECORDS:
- {"id":"gateway-L0002d","ts_us":1780014050101754,"service":"gateway","level":"error","event":"upstream_error","msg":"dependency orders returned 500","trace_id":"f74959cd495ecf77750c1b33563256fc","f":{"peer":"orders","status":500,"attempt":1}}
- {"id":"gateway-L0002e","ts_us":1780014050101754,"service":"gateway","level":"error","event":"order_failed","msg":"error handling request: order_failed","trace_id":"f74959cd495ecf77750c1b33563256fc","f":{"status":500}}

## Q14
STATEMENT: The same request that failed in shipping resulted in a 500 error at the api entry point, as shown by matching trace identifiers.
RECORDS:
- {"id":"api-S006c1","trace_id":"cf946236b46d003194ed4314d07bc772","span_id":"1042597a34180622","service":"api","kind":"server","op":"POST /orders","start_us":1790010046752301,"dur_us":136548,"status":500,"error":"delivery_failed"}
- {"id":"shipping-L00029","ts_us":1790010046778088,"service":"shipping","level":"error","event":"label_record_invalid","msg":"failed: label_record_invalid","trace_id":"cf946236b46d003194ed4314d07bc772","f":{"status":500}}

## Q15
STATEMENT: Broker detected upstream errors from shipping with status 500.
RECORDS:
- {"id":"broker-L00035","ts_us":1790007054376415,"service":"broker","level":"error","event":"upstream_error","msg":"dependency shipping returned 500","trace_id":"9e70bf315e3e45fe53c44567f1657285","f":{"peer":"shipping","status":500,"attempt":1}}

## Q16
STATEMENT: Broker worker limit reduced from the nominal 24 to 1
RECORDS:
- {"id":"broker-M000d","ts_us":1790015067187258,"service":"broker","window_s":5,"req":92,"err5xx":0,"p50_ms":289.454,"p99_ms":671.993,"queue_max":15,"busy_max":3,"workers_limit":1,"pool_in_use_max":0,"boots":1}

## Q17
STATEMENT: Ingest received 503 errors from broker
RECORDS:
- {"id":"ingest-L00033","ts_us":1790011047716067,"service":"ingest","level":"error","event":"upstream_error","msg":"dependency broker returned 503","trace_id":"31b14d308af0902e15a30805049ed75f","f":{"peer":"broker","status":503,"attempt":1}}

## Q18
STATEMENT: Orders' calls to payments timed out after ~1.2s because payments was slow/queued.
RECORDS:
- {"id":"orders-S00c32","trace_id":"797dc3ccf03edf66d31641446e554585","span_id":"8846bf9e499aaedd","parent_span_id":"27d7ec996e245b40","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780009054353215,"dur_us":1200000,"status":0,"error":"timeout","attempt":1}
- {"id":"orders-S00c33","trace_id":"09542ab5213a5b0dca499f21cde47a07","span_id":"bc53fc29d2d7ad83","parent_span_id":"b707bce7dd1ec69a","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780009054353422,"dur_us":1200000,"status":0,"error":"timeout","attempt":1}
- {"id":"orders-S00c34","trace_id":"11a52117cbff0dc8ae8bece2be98f16d","span_id":"d02d9b5bdba41ff9","parent_span_id":"270d478b6e9dd41a","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780009054550523,"dur_us":1200000,"status":0,"error":"timeout","attempt":1}
- {"id":"orders-S00c35","trace_id":"bedddd1078d1919de47fb18ec33af62e","span_id":"fa04c3d28e0bdc9c","parent_span_id":"e06e44f849729c0a","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780009054556556,"dur_us":1200000,"status":0,"error":"timeout","attempt":1}
- {"id":"orders-S00c36","trace_id":"ee891cd616a588b230ed691e91a5d861","span_id":"155d2353c3b81615","parent_span_id":"3a0b424051341d0d","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780009054605046,"dur_us":1200000,"status":0,"error":"timeout","attempt":1}

## Q19
STATEMENT: Payments returned 503 temporarily_unavailable errors.
RECORDS:
- {"id":"payments-L00037","ts_us":1780004061112888,"service":"payments","level":"error","event":"temporarily_unavailable","msg":"failed: temporarily_unavailable","trace_id":"b02ebf32098fc681379540b19b32ee5d","f":{"status":503}}

## Q20
STATEMENT: The gateway observed a rise in 5xx errors and latency, which is the user‑visible symptom.
RECORDS:
- {"id":"gateway-M000d","ts_us":1780002068048342,"service":"gateway","window_s":5,"req":91,"err5xx":62,"p50_ms":238.171,"p99_ms":303.841,"queue_max":0,"busy_max":11,"workers_limit":64,"pool_in_use_max":0,"boots":1}

## Q21
STATEMENT: Orders began returning 503 errors to its caller (gateway) as shown by gateway upstream_error logs at +101.763s.
RECORDS:
- {"id":"gateway-L00067","ts_us":1780017101762571,"service":"gateway","level":"error","event":"upstream_error","msg":"order step failed","trace_id":"15664e02125ddebd1ef7be6ce5174395","f":{"status":503}}

## Q22
STATEMENT: Billing service crashed due to OOM kill, stopping it from answering requests.
RECORDS:
- {"id":"billing-L0003b","ts_us":1790009059570367,"service":"billing","level":"fatal","event":"oom_kill","msg":"out of memory: process killed"}

## Q23
STATEMENT: Inventory service returned HTTP 500 responses due to internal errors such as stock_record_corrupt.
RECORDS:
- {"id":"inventory-L00018","ts_us":1780006025078148,"service":"inventory","level":"error","msg":"failed: internal_error","trace_id":"c74a31033447ed863441511d0cab49fa","f":{"status":500}}

## Q24
STATEMENT: Billing's database connection pool was exhausted, causing its charge service to return pool_exhausted errors.
RECORDS:
- {"id":"billing-L00042","ts_us":1790013069254249,"service":"billing","level":"error","msg":"timed out waiting for db connection","trace_id":"7ea71e25304dfd61c569150297c0cb08","f":{"pool_size":0}}
- {"id":"billing-L00043","ts_us":1790013069254249,"service":"billing","level":"error","event":"pool_exhausted","msg":"request failed: pool_exhausted","trace_id":"7ea71e25304dfd61c569150297c0cb08","f":{"status":503}}
- {"id":"billing-L00044","ts_us":1790013069330305,"service":"billing","level":"error","event":"pool_exhausted","msg":"db connection pool exhausted","trace_id":"1bc897238315a8e0e457ace237ba7858","f":{"pool_size":0}}
- {"id":"billing-L00045","ts_us":1790013069330305,"service":"billing","level":"error","event":"pool_exhausted","msg":"error handling request: pool_exhausted","trace_id":"1bc897238315a8e0e457ace237ba7858","f":{"status":503}}
- {"id":"billing-S0051b","trace_id":"7ea71e25304dfd61c569150297c0cb08","span_id":"28c2dcfc91dae735","parent_span_id":"25051631a6d59a4f","service":"billing","kind":"server","op":"charge","start_us":1790013068954249,"dur_us":300000,"status":503,"error":"pool_exhausted"}
- {"id":"billing-S0051c","trace_id":"1bc897238315a8e0e457ace237ba7858","span_id":"ef536d09518e900f","parent_span_id":"78e155d5a91aa623","service":"billing","kind":"server","op":"charge","start_us":1790013069030305,"dur_us":300000,"status":503,"error":"pool_exhausted"}
- {"id":"billing-S0051d","trace_id":"3770d587bd4e24dd16c8199b95c88512","span_id":"d869ea0a9f5ee6cb","parent_span_id":"3a5cbf9bb2e8d1c0","service":"billing","kind":"server","op":"charge","start_us":1790013069046814,"dur_us":300000,"status":503,"error":"pool_exhausted"}

## Q25
STATEMENT: Orders' calls to payments received connection refused errors and were retried without backoff.
RECORDS:
- {"id":"orders-S00a46","trace_id":"94a177aae56c833530637075702e9664","span_id":"fa8c84058504cb29","parent_span_id":"08a4255f2ff922c2","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780010048023407,"dur_us":2441,"status":-1,"error":"conn_refused","attempt":1}
- {"id":"orders-S00a47","trace_id":"94a177aae56c833530637075702e9664","span_id":"7ea96e9694c4dd05","parent_span_id":"08a4255f2ff922c2","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780010048125848,"dur_us":1925,"status":-1,"error":"conn_refused","attempt":2}
- {"id":"orders-S00a48","trace_id":"94a177aae56c833530637075702e9664","span_id":"0a70c9910af56560","parent_span_id":"08a4255f2ff922c2","service":"orders","kind":"client","op":"charge","peer":"payments","start_us":1780010048227773,"dur_us":2097,"status":-1,"error":"conn_refused","attempt":3}

## Q26
STATEMENT: Broker's configuration change set timeout.shipping_ms to 7 ms.
RECORDS:
- {"id":"ops-E0001","ts_us":1790019060838198,"source":"ops","type":"config_change","service":"broker","detail":{"key":"timeout.shipping_ms","old":1000,"new":7}}
- {"id":"broker-L00039","ts_us":1790019060922480,"service":"broker","level":"info","event":"config_changed","msg":"config reloaded timeout.shipping_ms","f":{"key":"timeout.shipping_ms","old":1000,"new":7}}

## Q27
STATEMENT: billing responses became much slower than baseline; callers timed out
RECORDS:
- {"id":"billing-S003df","trace_id":"a46cc300429b0d84b90237a0aae96c8a","span_id":"51da85c8d6624d98","parent_span_id":"a4d47d8f204644e2","service":"billing","kind":"server","op":"charge","start_us":1790002048903062,"dur_us":2744410,"status":200}
- {"id":"billing-S003da","trace_id":"5ae17b1b61157527a391e56e2f57fcf6","span_id":"03c4a43bb93fe064","parent_span_id":"b1d09d9fd2155cb6","service":"billing","kind":"server","op":"charge","start_us":1790002048985826,"dur_us":2399252,"status":200}

## Q28
STATEMENT: billing responses became much slower than baseline; callers timed out
RECORDS:
- {"id":"billing-S00389","trace_id":"b9e7a87b1cef18125f20db4773d1384c","span_id":"ee96b68bb196e9ae","parent_span_id":"9b40dd9c71d33376","service":"billing","kind":"server","op":"charge","start_us":1790011046282407,"dur_us":2258794,"status":200}
- {"id":"billing-S00388","trace_id":"f157cd782461a43502d925be86b178dd","span_id":"d0c8694a35d8374f","parent_span_id":"0aba743a190d02d0","service":"billing","kind":"server","op":"charge","start_us":1790011046459896,"dur_us":1982759,"status":200}

## Q29
STATEMENT: billing responses became much slower than baseline; callers timed out
RECORDS:
- {"id":"billing-S003e0","trace_id":"d62c6c8663e956ee81108070130d5f39","span_id":"e8b6163ae819e347","parent_span_id":"4a5c24144f55a213","service":"billing","kind":"server","op":"charge","start_us":1790018054890018,"dur_us":1634820,"status":200}
- {"id":"billing-S003ef","trace_id":"db60998450f5220d1a6269218660cf7b","span_id":"a4a147654e8dec79","parent_span_id":"f82f32ced381dc1e","service":"billing","kind":"server","op":"charge","start_us":1790018054967541,"dur_us":2785661,"status":200}

## Q30
STATEMENT: Ingest timed out waiting for broker, returning upstream_timeout to api.
RECORDS:
- {"id":"ingest-S00919","trace_id":"0ea48e0701ca216247e34d88a7ad0c3d","span_id":"c7f8273085c746bd","parent_span_id":"cb04bdd25d5b5771","service":"ingest","kind":"server","op":"submit_order","start_us":1790016062859214,"dur_us":2867689,"status":504,"error":"upstream_timeout","queue_us":363099}
- {"id":"api-S0091a","trace_id":"01c14be986a9d0d8f502784df4b26020","span_id":"dcb3387404b521d3","service":"api","kind":"server","op":"POST /orders","start_us":1790016061934762,"dur_us":2508160,"status":504,"error":"upstream_timeout"}
