# FR-4.10

| ID | Requirement | |
|---|---|---|
| FR-4.10 | Whether a state file says which kind of absence its missing context is. FR-1.11g omits an empty value, so a payload with no context block writes a file with no context keys, indistinguishable from a session whose window has not been measured yet. silo's board met this reading a file written from a stub payload. Adding a key is compatible under FR-1.11o, so the cheap answer is probably a key. | [?] |
