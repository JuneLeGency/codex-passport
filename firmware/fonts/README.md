# Device font

The read-only progress cards use a 16 px, 2 bpp bitmap derived from Noto Sans CJK SC Regular under [SIL OFL 1.1](OFL.txt).
`python scripts/build_font.py` downloads the SHA256-verified official font at a pinned commit and runs `lv_font_conv@1.5.3`.
Generated C and the source OTF stay in ignored `firmware/generated/`; they are separate from our original application implementation.

Coverage includes Latin, general punctuation, arrows, CJK punctuation/kana, basic CJK U+4E00–U+9FFF and fullwidth forms.
Spaces are supported. Unsupported emoji and characters are omitted at display time rather than rendered as missing-glyph boxes; the phone retains the bounded original summary.
Text is static, single-line and ellipsized; there is no marquee, detail menu or automatic scrolling.
