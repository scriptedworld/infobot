# FR-1.11r

| ID | Requirement | |
|---|---|---|
| FR-1.11r | A string value comes back the bytes it went in as, or the write fails loudly. Every code point a strict reader rejects or silently alters is escaped: C0, DEL, C1, and the two line separators U+2028 and U+2029. The separators are in the set because YAML 1.1 makes all five of LF, CR, U+0085, U+2028 and U+2029 line breaks and 1.2 cuts the set to LF and CR, so which of them fold depends on the reader's version rather than on the character. A range derived from one parser's behaviour escaped U+0085 and left its two spec siblings raw. The double-quoted style already carries the full escape set on one line, so this needs no change of form and alters no byte of any value not holding one. Unescaped, `\n` and `\r` and U+0085 each come back as a space, which no reader can detect; the rest make a file no parser will read, which is the loud failure this keeps. | [A/D] |
