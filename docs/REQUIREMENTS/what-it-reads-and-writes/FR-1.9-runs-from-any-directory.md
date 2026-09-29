# FR-1.9

| ID | Requirement | |
|---|---|---|
| FR-1.9 | It runs from any working directory, with nothing that can be half present. Claude Code invokes it by absolute path from wherever the session happens to be, so the entry point resolves what it runs from its OWN location rather than from the working directory or from `PATH`. | [D] |
