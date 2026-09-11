# Attadipa naming

**The name is written two ways and the split is deliberate.**

- **`Atta-dipa`** — everywhere a person reads it as a name: the website, the
  README, logos and wordmarks, app and PWA names, marketing copy, `og:title`,
  the `<title>` tag, alt text, `aria-label`.
- **`Attadipa`** — everywhere a machine reads it: the repository name, URLs and
  the Pages path, C++ namespaces, CMake targets, macros, include guards, header
  paths, storage and artifact prefixes, CI job and workflow text, and the
  `attadipa-agent-task` markers.

Both are the project name. Neither is a rename of the other, and a file often
carries both: `docs/index.html` says `Atta-dipa` in the title and
`https://hleserg.github.io/Attadipa/` in the canonical link on the next line.
When in doubt, ask who reads the string — a reader gets the hyphen, a parser
does not.

The hyphen is not decoration. `Attadipa` reads as one long word and people
stress it wrongly on first sight; `Atta-dipa` shows the two parts of the Pali
compound, which is also where the meaning lives.

`docs/assets/site.js` is the trap this rule exists for. It assigns the head
strings on every JavaScript-enabled load, so a copy table that still says
`Attadipa` silently overwrites correct static HTML and is what a rendering
crawler indexes. `tools/site/check_head_sync.py` compares the two and is a
required check; change both files together.

Always write either form with this capitalization; neither is an abbreviation,
and `AttadipaOS` is not an alternative official name.

The name comes from the Pali *attadīpa* ("relying on oneself" or "having
oneself as an island/refuge"). The brand and code deliberately omit the
diacritic: `Attadipa`.

**Independent by design** is the architectural credo that follows from that
meaning: capability that can reasonably run on the device must not require a
phone, cloud service, or persistent Internet connection merely for
architectural convenience.

Lumar is Attadipa's firefly mascot. `firefly` remains appropriate only as the
ordinary English word for the insect, never as the old project brand or a
technical identifier.

Technical naming follows these forms:

- C++ namespace: `attadipa`
- CMake project, targets, and aliases: `attadipa`, `attadipa_*`, `attadipa::*`
- macros and include guards: `ATTADIPA_*`
- public headers: `<attadipa/...>`
- web storage and generated artifact prefixes: `attadipa-`

In Russian prose, write **Аттадипа**; recommended pronunciation is
**атта-ДИ-па**. Keep the Latin `Atta-dipa` wordmark in UI and logo use — it is
user-facing, so it takes the hyphen.
