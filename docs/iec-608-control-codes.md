# ANSI/CTA-608-E — Tables 49–53 (Markdown)

Source: ANSI/CTA-608-E (Annex F, FCC Rules Excerpts) :contentReference[oaicite:0]{index=0}

---

## Table 49 — Special Characters

**Note:** Each hex value must be preceded by `0x11` (Data Channel 1) or `0x19` (Data Channel 2).

| Hex | Glyph | Description |
|-----|-------|-------------|
| 30 | ® | Registered mark |
| 31 | ° | Degree sign |
| 32 | ½ | One-half |
| 33 | ¿ | Inverted question mark |
| 34 | ™ | Trademark |
| 35 | ¢ | Cents sign |
| 36 | £ | Pound sterling |
| 37 | ♪ | Music note |
| 38 | à | Lower-case a with grave |
| 39 | ␠ | Transparent space |
| 3A | è | Lower-case e with grave |
| 3B | â | Lower-case a with circumflex |
| 3C | ê | Lower-case e with circumflex |
| 3D | î | Lower-case i with circumflex |
| 3E | ô | Lower-case o with circumflex |
| 3F | û | Lower-case u with circumflex |

---

## Table 50 — Standard Characters

### 0x20–0x3F

| Hex | Glyph | Description |
|-----|-------|-------------|
| 20 | ␠ | Space |
| 21 | ! | Exclamation mark |
| 22 | " | Quotation mark |
| 23 | # | Number sign |
| 24 | $ | Dollar sign |
| 25 | % | Percent sign |
| 26 | & | Ampersand |
| 27 | ' | Apostrophe |
| 28 | ( | Open parenthesis |
| 29 | ) | Close parenthesis |
| 2A | á | Lower-case a with acute |
| 2B | + | Plus sign |
| 2C | , | Comma |
| 2D | - | Hyphen |
| 2E | . | Period |
| 2F | / | Slash |
| 30–39 | 0–9 | Digits |
| 3A | : | Colon |
| 3B | ; | Semicolon |
| 3C | < | Less-than |
| 3D | = | Equals |
| 3E | > | Greater-than |
| 3F | ? | Question mark |

### 0x40–0x5F (Uppercase)

| Hex | Glyph | Description |
|-----|-------|-------------|
| 40 | @ | At sign |
| 41–5A | A–Z | Upper-case letters |
| 5B | [ | Open bracket |
| 5C | é | Lower-case e with acute |
| 5D | ] | Close bracket |
| 5E | í | Lower-case i with acute |
| 5F | ó | Lower-case o with acute |

### 0x60–0x7F (Lowercase & Symbols)

| Hex | Glyph | Description |
|-----|-------|-------------|
| 60 | ú | Lower-case u with acute |
| 61–7A | a–z | Lower-case letters |
| 7B | ç | Lower-case c with cedilla |
| 7C | ÷ | Division sign |
| 7D | Ñ | Upper-case N with tilde |
| 7E | ñ | Lower-case n with tilde |
| 7F | ■ | Solid block |

---

## Table 51 — Mid-Row Codes

| Data Ch 1 | Data Ch 2 | Attribute |
|-----------|-----------|-----------|
| 11 20 | 19 20 | White |
| 11 21 | 19 21 | White + underline |
| 11 22 | 19 22 | Green |
| 11 23 | 19 23 | Green + underline |
| 11 24 | 19 24 | Blue |
| 11 25 | 19 25 | Blue + underline |
| 11 26 | 19 26 | Cyan |
| 11 27 | 19 27 | Cyan + underline |
| 11 28 | 19 28 | Red |
| 11 29 | 19 29 | Red + underline |
| 11 2A | 19 2A | Yellow |
| 11 2B | 19 2B | Yellow + underline |
| 11 2C | 19 2C | Magenta |
| 11 2D | 19 2D | Magenta + underline |
| 11 2E | 19 2E | Italics |
| 11 2F | 19 2F | Italics + underline |

---

## Table 52 — Miscellaneous Control Codes

| Data Ch 1 | Data Ch 2 | Mnemonic | Description |
|-----------|-----------|----------|-------------|
| 14 20 | 1C 20 | RCL | Resume Caption Loading |
| 14 21 | 1C 21 | BS | Backspace |
| 14 22 | 1C 22 | AOF | Reserved (Alarm Off) |
| 14 23 | 1C 23 | AON | Reserved (Alarm On) |
| 14 24 | 1C 24 | DER | Delete to End of Row |
| 14 25 | 1C 25 | RU2 | Roll-Up Captions (2 rows) |
| 14 26 | 1C 26 | RU3 | Roll-Up Captions (3 rows) |
| 14 27 | 1C 27 | RU4 | Roll-Up Captions (4 rows) |
| 14 28 | 1C 28 | FON | Flash On |
| 14 29 | 1C 29 | RDC | Resume Direct Captioning |
| 14 2A | 1C 2A | TR | Text Restart |
| 14 2B | 1C 2B | RTD | Resume Text Display |
| 14 2C | 1C 2C | EDM | Erase Displayed Memory |
| 14 2D | 1C 2D | CR | Carriage Return |
| 14 2E | 1C 2E | ENM | Erase Non-Displayed Memory |
| 14 2F | 1C 2F | EOC | End of Caption (Flip Memories) |
| 17 21 | 1F 21 | TO1 | Tab Offset 1 Column |
| 17 22 | 1F 22 | TO2 | Tab Offset 2 Columns |
| 17 23 | 1F 23 | TO3 | Tab Offset 3 Columns |

---

## Table 53 — Preamble Address Codes (PACs)

### First Byte (Row Selection)

| Row | Data Ch 1 | Data Ch 2 |
|-----|-----------|-----------|
| 1 | 11 | 19 |
| 2 | 11 | 19 |
| 3 | 12 | 1A |
| 4 | 12 | 1A |
| 5 | 15 | 1D |
| 6 | 15 | 1D |
| 7 | 16 | 1E |
| 8 | 16 | 1E |
| 9 | 17 | 1F |
| 10 | 17 | 1F |
| 11 | 10 | 18 |
| 12 | 13 | 1B |
| 13 | 13 | 1B |
| 14 | 14 | 1C |
| 15 | 14 | 1C |

### Second Byte (Color / Indent)

| Attribute | Even Rows | Odd Rows |
|-----------|-----------|----------|
| White | 40 | 60 |
| White + underline | 41 | 61 |
| Green | 42 | 62 |
| Green + underline | 43 | 63 |
| Blue | 44 | 64 |
| Blue + underline | 45 | 65 |
| Cyan | 46 | 66 |
| Cyan + underline | 47 | 67 |
| Red | 48 | 68 |
| Red + underline | 49 | 69 |
| Yellow | 4A | 6A |
| Yellow + underline | 4B | 6B |
| Magenta | 4C | 6C |
| Magenta + underline | 4D | 6D |
| White italics | 4E | 6E |
| White italics + underline | 4F | 6F |
| Indent 0 | 50 | 70 |
| Indent 4 | 52 | 72 |
| Indent 8 | 54 | 74 |
| Indent 12 | 56 | 76 |
| Indent 16 | 58 | 78 |
| Indent 20 | 5A | 7A |
| Indent 24 | 5C | 7C |
| Indent 28 | 5E | 7E |

**Note:** All indent PACs force **white** as the color attribute.

---
