# FR-1.11h

| ID | Requirement | |
|---|---|---|
| FR-1.11h | `context_remaining` is derived rather than read, and never negative. `context_percent` is neither floored nor capped, so an over-full window reads as over-full where the bar can only draw it as full. | [D] |
