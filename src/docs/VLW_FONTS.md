# VLW Font Generation Plan

This document defines the target `.vlw` font files for the ESP32 weather station UI
and the exact character sets to include when generating them.

## Goal

Replace `OpenFontRender` + embedded font arrays with `TFT_eSPI` smooth fonts (`.vlw`)
stored in LittleFS.

Important requirements:

- Keep the two largest fonts as minimal subsets.
- Use a shared Unicode-aware UI character set for the remaining sizes.
- Support Polish characters directly from weather JSON and UI labels.
- Support the micro sign `µ` for radiation units.
- Remove mojibake workarounds from the codebase later.

## Target Files

Generate these files:

1. `clock_36.vlw`
2. `temp_48.vlw`
3. `ui_14.vlw`
4. `ui_16.vlw`
5. `ui_18.vlw`
6. `ui_20.vlw`
7. `ui_24.vlw`

Suggested LittleFS paths:

- `/fonts/clock_36.vlw`
- `/fonts/temp_48.vlw`
- `/fonts/ui_14.vlw`
- `/fonts/ui_16.vlw`
- `/fonts/ui_18.vlw`
- `/fonts/ui_20.vlw`
- `/fonts/ui_24.vlw`

## Character Sets

### 1. `clock_36.vlw`

Use only:

```text
0123456789:
```

### 2. `temp_48.vlw`

Use only:

```text
0123456789-.,°C
```

Notes:

- Include `°` because the temperature label will use the degree sign directly.
- Include `C` because temperatures may be shown as `°C`.

### 3. Shared UI character set for `ui_14`, `ui_16`, `ui_18`, `ui_20`, `ui_24`

Use this exact set:

```text
ABCDEFGHIJKLMNOPQRSTUVWXYZ
abcdefghijklmnopqrstuvwxyz
ĄĆĘŁŃÓŚŹŻ
ąćęłńóśźż
0123456789
 .,:;!?+-/%()[]=_"'°µ
```

This covers:

- Polish weekday names and abbreviations
- `Słońce`
- city names
- weather descriptions from JSON
- AQI / PM labels
- radiation values like `0.063 µSv/h`
- debug text

## Recommended Font Family

Choose one TTF family and generate all 7 `.vlw` files from it.

Recommended order of preference:

1. `Arial Narrow`
2. `Trebuchet MS`
3. another humanist sans with good Polish glyph support

Reasoning:

- narrow / compact families fit the current layout better
- they reduce the risk of the UI "spreading out" horizontally

## Size Mapping

Map generated sizes exactly like this:

- `ui_14.vlw` -> for current size `14`
- `ui_16.vlw` -> for current size `16`
- `ui_18.vlw` -> for current size `18`
- `ui_20.vlw` -> for current size `20`
- `ui_24.vlw` -> for current size `24`
- `clock_36.vlw` -> for current size `36`
- `temp_48.vlw` -> for current size `48`

## Current UI Usage Map

### `ui_14.vlw`

Used for:

- `Odczuwalna`
- debug buttons like `BACK`, `PAA`, `BfS`
- small helper labels

### `ui_16.vlw`

Used for:

- top date line
- radiation value
- other small values aligned in the layout

### `ui_18.vlw`

Used for:

- humidity
- pressure
- wind
- sunrise / sunset times
- AQI rows
- most debug text
- feels-like value

### `ui_20.vlw`

Used for:

- lower section headers like `Słońce`, `RAD`
- city name in lower section

### `ui_24.vlw`

Used for:

- boot progress text
- weather description
- forecast weekdays

### `clock_36.vlw`

Used for:

- current time `HH:MM`

### `temp_48.vlw`

Used for:

- main current temperature
- fallback placeholder temperature

## Generator Workflow

The `.vlw` files should be generated with the `TFT_eSPI` Processing tool:

- `Tools/Create_Smooth_Font/Create_font.pde`

This tool is included in the `TFT_eSPI` library distribution.

## Manual Checklist Before Migration

After generating the fonts but before final integration, the codebase should be cleaned up:

1. Replace mojibake sequences like `Â°` with real Unicode characters.
2. Replace ASCII fallback strings with proper Polish:
   - `Slonce` -> `Słońce`
   - `SR` -> `ŚR`
   - weekday names with diacritics
3. Remove weather-description sanitizing fallback once UTF-8 rendering is confirmed.
4. Switch radiation unit from `uSv/h` to `µSv/h`.

## Suggested Output Folder On Disk

If you generate the files outside the project first, copy them here afterwards:

```text
data/fonts/
```

Expected final contents:

```text
data/fonts/clock_36.vlw
data/fonts/temp_48.vlw
data/fonts/ui_14.vlw
data/fonts/ui_16.vlw
data/fonts/ui_18.vlw
data/fonts/ui_20.vlw
data/fonts/ui_24.vlw
```

## Visual Test String

Before generating all fonts, first generate only:

- `ui_20.vlw`
- `ui_16.vlw`

Use the shared UI character set and visually verify this test string:

```text
Słońce ŚR Kielce Dauchingen
Odczuwalna 12.3°C
RAD 0.063 µSv/h
Poniedziałek Środa Czwartek
Zażółć gęślą jaźń
0123456789 : - . / % ( )
```

What to check on the LCD:

1. `ł`, `ń`, `Ś`, `ż`, `ź`, `ę`, `ą` are visible and distinct.
2. `µ` displays correctly in `µSv/h`.
3. vertical strokes in letters like `k`, `l`, `h`, `t` are not too thin.
4. spacing between letters is readable and not visually collapsed.
5. digits remain clear enough for time and temperature.

If the font fails any of the checks above, switch to another TTF family before
generating the full set of 7 `.vlw` files.
