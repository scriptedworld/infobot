# FR-1.11q

| ID | Requirement | |
|---|---|---|
| FR-1.11q | A number is spelled without an exponent at any magnitude. A float carries a decimal point so a reader gets a float back, and an integer does not. `1e+06` is a legal spelling of a million and a reader matching `[0-9.]+` captures `1` from it, which is a plausible small number rather than a parse failure, so an exponent is a silent wrong answer rather than a formatting preference. | [A/D] |
