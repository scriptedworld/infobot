# FR-1.11g

| ID | Requirement | |
|---|---|---|
| FR-1.11g | The file carries `session` and `written` always; `cwd`, `model` and `effort` where the payload names them; and `context_used`, `context_size`, `context_remaining` and `context_percent` where the window can be measured. A key whose value is empty is omitted rather than written blank. Keys are quoted, sorted and one to a line. A value's type survives the round trip, so a strict reader gets back a string, an integer and a float. `written` is ISO 8601 carrying an offset, to the second. | [D] |
