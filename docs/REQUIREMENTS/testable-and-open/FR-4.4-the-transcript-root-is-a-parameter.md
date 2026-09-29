# FR-4.4

| ID | Requirement | |
|---|---|---|
| FR-4.4 | The transcript root is a parameter and the rate table and the offsets follow the XDG variables, so section 8 is tested against a fixture tree with nothing patched. A test giving a root must move `XDG_STATE_HOME` too, or the offsets it writes land beside the real ones. | [D] |
