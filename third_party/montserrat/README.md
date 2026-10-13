# Montserrat

TTF sources used to generate `main/font_montserrat.c`.

- Family: [Montserrat](https://fonts.google.com/specimen/Montserrat)
- License: SIL Open Font License 1.1
- Upstream: https://github.com/JulietaUla/Montserrat

Regenerate glyphs:

```sh
python3 tools/gen_montserrat.py \
  --medium third_party/montserrat/Montserrat-Medium.ttf \
  --semibold third_party/montserrat/Montserrat-SemiBold.ttf \
  --bold third_party/montserrat/Montserrat-Bold.ttf \
  --out main/font_montserrat.c
```
