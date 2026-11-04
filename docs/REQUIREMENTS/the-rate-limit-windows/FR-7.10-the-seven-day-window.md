# FR-7.10

| ID | Requirement | |
|---|---|---|
| FR-7.10 | The status line reads one rate limit window, `seven_day`, and its length is seven days, taken from the payload's own field name. No other window is read: a `five_hour` entry in `rate_limits`, whether absent, null, zero or carrying a percentage and a reset time, renders nothing and changes nothing else that is rendered. | [A/D] |
