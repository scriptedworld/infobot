# FR-8.29

| ID | Requirement | |
|---|---|---|
| FR-8.29 | Cache reads and cache writes are priced apart rather than lumped together as cache. A read is charged at the model's own read multiplier where the rate table gives one, and at the table's `cache_read` where it does not; a write costs more than a fresh input token. | [D] |
