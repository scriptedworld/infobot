# FR-3.11

| ID | Requirement | |
|---|---|---|
| FR-3.11 | A host that does not answer costs a bounded wait and then counts as unknown. Killing the process is not the bound: a host is a script, and killing the shell leaves any child it spawned holding the inherited stdout pipe, so the read blocks on the grandchild. The bound is the two second timeout plus a short delay after which the pipes are closed regardless. | [D] |
