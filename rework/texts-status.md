# Reworked texts: status

The text list `rework/texts.md` is applied in PR #422 (branch
`texts-reworked`, from main 453cf334). State when the PR was opened:

- 848 English and 897 German strings applied as listed. The only edits to
  the list's wording: four BUS FAULT continuation lines lose their Markdown
  backticks, and the OUTPUTS tile's clash label is the 14-cell short form
  (owner's choice).
- Every rendered string fits (`render_ui.py --fit --lang all`); host suite
  128/128; frame-cost budgets hold.
- Owner decisions taken: two-row alert band for long alerts; SERVO
  PROGRAMMER notes page (1/3, tap for the next) instead of dropping hints;
  the German register is the du-imperative with Gas, Pulsrate, Verbindung,
  and `ui_text_de.c` and `docs/Language.md` say so.
- Not measured here: the firmware images and the panel stack depth; CI
  measures them on #422.
