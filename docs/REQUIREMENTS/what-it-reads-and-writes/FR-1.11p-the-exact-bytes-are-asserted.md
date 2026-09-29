# FR-1.11p

| ID | Requirement | |
|---|---|---|
| FR-1.11p | infobot asserts the exact bytes it emits, against the pack that emits them, so a change in wrench's canonical form surfaces here as a failing test and is announced under FR-1.11o. The assertion lives in this repository, since these bytes are infobot's contract with its readers and a check needing a sibling checkout fails for the wrong reason on a fresh clone. Why one emitter: `docs/DECISIONS/the-state-file-is-emitted-by-wrenchs-pack.md`. | [A/D] |
