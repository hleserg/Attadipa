# SEO — what was audited, what changed, and what is deliberately not done

Audited 2026-08-23 against `docs/` (the GitHub Pages site at
<https://hleserg.github.io/Attadipa/>), `README.md`, `README.ru.md` and the
repository's own metadata. This file is the record: what the state was, what
changed, and — the part that matters more — the claims that were **not** made
because they are not true yet.

## 0. The constraint this whole document works under

Attadipa is at early implementation. Six libraries and a simulator build and
pass 24 host tests; **the firmware has not run on a board yet — only bench
code has.** Every line below was written so that a reader arriving from a
search engine learns that in the first screenful rather than the fifth.

**That sentence used to be three sentences, and the broadest of them went
false.** The page said *"not yet run on hardware"* in the meta description,
*"no board has run it yet"* on the card and in the manifest, and *"no Attadipa
firmware has yet run on a physical board"* in the JSON-LD. On 2026-08-23 bench
probes written for this project ran on the Waveshare unit — ESP-IDF images
loaded out of RAM, writing nothing, `verify-flash` clean over all 33 554 432
bytes — and produced
measurements: the IMU at `0x6B` and not `0x6A`, `REVISION_ID 0x7C`, gravity at
1.03 g, the touch controller answering `0x64` after a 10 ms reset pulse, the
AXP2101 rails read raw (`STATUS.md` §*The bench session of 2026-08-23*,
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md)). Two of
the three wordings were then untrue and one was still exact. **Three wordings
meaning three different things is the tell** — a deliberate distinction would
have been one sentence written six times, which is what it is now. The claim
kept is the narrow one, and it is checked: `check_head_sync.py` holds the HTML
and `site.js` to the same string, so the six copies cannot drift apart again
even if the sentence needs replacing a third time. Found in review, on the
branch whose subject is claims about verification.

Concretely, that ruled out: "the best smartwatch OS", any superlative, any
feature described as working that has only been designed, any benchmark, and any
metric — stars, downloads, users — dressed up as adoption. Both the
`description` meta tag and `og:description` end
*"Early stage — the firmware has not run on a board yet; only bench code has."*
It costs click-through and it is the correct trade.

## 1. The search niche, as it actually is

The realistic terms this project can rank for, roughly in descending order of
how winnable each is:

| Term | Why it fits | Competition |
|---|---|---|
| `esp32-s3 smartwatch firmware` | exact description of the artefact | thin — a handful of projects |
| `t-watch s3 plus firmware` / `lilygo t-watch open source` | one of the two target boards, named | thin, and mostly forum threads |
| `waveshare esp32-s3-touch-amoled-2.06 firmware` | the other target; almost nothing exists | nearly empty |
| `meshcore firmware` / `lora mesh smartwatch` | MeshCore is the transport | small and growing |
| `offline gnss smartwatch open source` | the differentiating feature | moderate |
| `open source smartwatch os` | the category a newcomer searches | crowded — AsteroidOS, Bangle.js, InfiniTime, Wasp-OS |
| `lvgl smartwatch ui` | the UI layer | moderate, mostly tutorials |

**`smartwatch OS` is used as a search-category term only**, in the JSON-LD
`keywords` field and the repository topics — never as a technical claim in prose.
Attadipa is firmware and an application platform on FreeRTOS; it is not an
operating system, and no sentence on the site or in either README says it is.

The four crowded-term incumbents above are the reason the honest framing is also
the strategic one: against AsteroidOS or InfiniTime, "another smartwatch OS" is
an unwinnable claim, while "the only firmware targeting *these two specific
boards*, with the hardware facts traced to datasheets" is a claim nobody else is
making.

## 2. What changed

### `docs/index.html` — `<head>`

| Before | After |
|---|---|
| `<title>Attadipa — Independent by design</title>` — carried no term anybody searches for | `Attadipa — open-source ESP32-S3 smartwatch firmware, LoRa mesh, offline GNSS`, with the terms front-loaded so they survive truncation at ~60 characters |
| `description` 154 chars, ended on "product-grade UI" | 151 chars, names LoRa MeshCore / GNSS / LVGL / FreeRTOS and closes on the early-implementation caveat |
| no `robots` directive | `index, follow, max-image-preview:large, max-snippet:-1` — the image directive is what allows a large thumbnail in results |
| `og:` had type, title, description, image, url | added `og:site_name`, `og:locale`, `og:locale:alternate`, `og:image:type`, `og:image:width`, `og:image:height`, `og:image:alt` |
| `twitter:card` alone | added `twitter:title`, `twitter:description`, `twitter:image`, `twitter:image:alt`. Without an image URL the `summary_large_image` card had nothing to render and silently degraded |
| no structured data | a JSON-LD `@graph`: `WebSite` + `Person` + `SoftwareSourceCode`, with `targetProduct` carrying the feature list, `license` pointing at MIT and `isAccessibleForFree: true` |

The JSON-LD deliberately uses **`SoftwareSourceCode`** as the primary type rather
than `SoftwareApplication`. The thing at this URL is a source repository, not an
installable application; typing it as an application would invite a rich result
that implies a downloadable product exists. No `aggregateRating` and no
`interactionStatistic` — both would be fabricated.

### Images — the largest single win

| File | Was served | Now served | Saved |
|---|---|---|---|
| hero banner | `attadipa-banner.png`, **1.32 MB** | `banner.webp`, **43 KB**, identical 1774 × 887 | 1.28 MB |
| design board | `attadipa-style-board.png`, **1.70 MB** | `style-board.webp`, **150 KB**, identical 1448 × 1086 | 1.55 MB |

Both `.webp` files were already in the repository and simply unreferenced. That
is **2.8 MB off the first page view**, which on a mobile connection is the
difference between a Largest Contentful Paint a search engine counts as good and
one it counts as poor.

Three `width`/`height` pairs were also wrong — the banner, the design board and
the mascot were each declared at a size their file never had. That is a
layout-shift source rather than a cosmetic error: the browser reserves the
declared box and then reflows when the real image arrives.

What each image actually is, is deliberately not restated here.
`tools/site/check_site_facts.py` reads the dimensions out of the PNG, JPEG and
WebP headers themselves and compares them with the `width`/`height` in
`docs/index.html` on every CI run, so the numbers are asserted where a browser
uses them rather than copied into a document that cannot notice when they drift.
It compares proportion rather than scale: `favicon.png` is 64 × 64 and the page
draws it at 34 and again at 28, which reserves exactly the right box and is not
a defect. The one exception is `og:image:width` / `og:image:height`, compared
**exactly** — a card renderer has no page to scale them against, so they are the
file's own numbers or they are wrong. Added after review pointed out that the
card's dimensions were typed numbers about a file the check already had open.

Loading hints added: `fetchpriority="high"` on the hero banner (it *is* the LCP
element), `loading="lazy"` on everything below the fold, `decoding="async"`
throughout.

Two `alt` texts were also rewritten from labels into descriptions — *"Attadipa
visual design board"* now says what is actually on the board. `alt=""` is kept
on the two decorative images, which is correct: an empty `alt` tells a screen
reader to skip a decoration, and inventing text for one is worse than none.

### `docs/assets/site.js` — the head is rewritten at run time, and it had the old strings

This is the finding that would have silently undone the whole pass, and it was
caught by review rather than by writing. `site.js` is `defer`-loaded and its
`setLanguage()` assigns `document.title`, the `description` and `og:description`
**unconditionally on every load**, from a hardcoded `copy` object. That object
still held the pre-audit strings. Googlebot renders JavaScript, so the rendered
DOM — the one that gets indexed — would have carried the old title and the old
description no matter what `index.html` said.

The mechanism itself is right: a Russian visitor should get a Russian title and
a Russian description, and the page has no other way to do that. So the fix is
to keep the strings in step, and the code now says so where somebody will read
it before editing the head again. `og:title`, `og:locale`, `twitter:title` and
`twitter:description` were added to the same assignment, because a language
switch that updates four of eight head fields leaves the page in a state neither
language describes.

**The rule this leaves behind:** `index.html`'s head and `site.js`'s `copy`
object are one thing in two files, and hand-verification is not enough to keep
them that way. The first version of this section said they had been verified by
hand *and did match*; review found that two of the eight fields did not — the
fix for the title defect had assigned the meta description to `og:description`
as well, putting the search-result string on the social card for every crawler
that runs JavaScript while the non-rendering ones read the purpose-written one
from the HTML. One URL, two card texts, and a sentence in this document saying
it had been checked.

So it is a check now, not a claim: `tools/site/check_head_sync.py`, run by CI in
the *Documentation consistency* job. It has **two halves, and neither can see
the other's defect.**

The **data** half extracts the English `<title>` and seven `<meta>` contents from
`index.html` — eight fields — and the same fields from `site.js`'s `copy.en`, and
reports every divergence it finds, naming the field and both values. The two
`twitter:` tags are checked against the `og:` strings they mirror, and `copy.ru`
is checked for presence, since the Russian strings have no counterpart in the
HTML by construction.

The **wiring** half reads `setLanguage()` itself. It exists because the two
historical defects are not the same kind, which the first version of this
section did not notice either. The stale title is a *data* divergence and a
string comparison finds it. The `og:description` overwrite is a *wiring* defect:
it leaves every string in both files byte-identical, so the comparison exits 0
on it. Reverting `site.js:75` from `.cardDescription` to `.description`
reproduces that defect exactly, and four more one-token reversions do the same
for `twitter:description`, `og:title`, `twitter:title` and
`og:locale:alternate` — the last being the state this branch fixed. So the check
now carries a table of *which `copy` field must be assigned into which tag*, and
asserts `setLanguage()` against it, including which DOM element each variable
selects. Found in review, in the artifact built to prevent exactly this.

**A third kind of divergence lives inside `index.html` alone**, and review found
it after the other two were closed. Three head strings are duplicated by hand
and assigned by nothing: `og:image` ↔ `twitter:image`, `og:image:alt` ↔
`twitter:image:alt`, and the meta description ↔ the JSON-LD
`WebSite.description` fifty-odd lines below it. Both halves above exit 0 on
every one of them, because neither file disagrees with the other — the file
disagrees with itself. They are now a declared table, compared on every run,
**and the table has a completeness rule of its own**: any two head strings of
24 characters or more that are byte-identical and not declared as a pair are
reported, so the next duplicate is an error the day it is added rather than the
day someone edits one half. The 24-character floor keeps `en_US` and its like
out of it.

Its own mutation tests (`tools/site/test_check_head_sync.py`, **44 cases**) run
first: **34 break the pair and require the check to fail, 10 leave it valid and
require the check to stay quiet.** Every one of the 34 break the pair cases was written against a
version of the checker that let that exact mutation through — that is what makes
them tests rather than description. A checker that passes everything is worse
than none: it is what the next agent trusts instead of re-checking, which is
exactly how this section came to be wrong.

An earlier draft said *"seven of them fail against the check as it was"*. That
counted the wiring cases in a 29-case suite and was never recounted as the suite
grew, so it had stopped naming a set — review caught it. Splitting the count by
what each case asserts is the version that stays true when a case is added,
because the two numbers are read off the file rather than remembered.

### `docs/index.html` — a `<noscript>` fallback for `.reveal`

Not an SEO fix, found while tracing what the script does to the head. Every
section below the hero carries `.reveal`, which shipped at `opacity:0` in the
stylesheet; `site.js` adds `.visible` through an `IntersectionObserver` as each
scrolls into view. With scripting off nothing ever added it, so a document that
contains the whole page rendered as a hero and empty space. Googlebot runs
scripts and never saw it.

The first fix here was a `<noscript>` block restoring `opacity:1`, and review
was right that it did not cover the case this section had claimed for it. A
reader *whose script request failed* has scripting **enabled**, so `<noscript>`
does not apply to them; they get the stylesheet's `opacity:0` and no observer,
which is the blank page again. Nor does `<noscript>` help if the script loads
and throws before reaching the observer — which had already happened once on
this page.

**So hiding is now opt-in, and the script is what opts in.** `.reveal` ships
visible; `.js-reveal` on `<html>` is what makes it `opacity:0`, and `site.js`
adds that class inside the same branch that creates the observer. Every way the
animation can fail to run — scripting off, a failed request, a throw before
that line, no `IntersectionObserver`, reduced motion — now leaves the page
visible, because the thing that hides the content and the thing that reveals it
are the same statement. The `<noscript>` block is kept as a belt-and-braces
override for the scripting-off case and is no longer what the argument rests on.

**The inversion has a cost, and review named it before a visitor did.** `.reveal`
carries `transition:opacity .65s` unconditionally and `site.js` is `defer`red. On
a fast load the script runs before first paint and nobody sees anything. On a
slow one — cold cache, poor connection, a delayed asset — the page paints fully
visible, and *then* adding `js-reveal` fades every section out and slides it
18px down before the observer brings the in-view ones back. Nothing faded out
before this change; the honest description of the trade is that the blank page
was replaced by a flicker on slow loads.

So the flicker is gone too: `site.js` asks the Paint Timing API whether
`first-contentful-paint` has already been recorded, and if it has, skips the
reveal entirely and leaves the page as painted. The animation is decorative and
the readable page is not, and an animation that arrives after the content has
been read buys nothing worth a visible flicker. Where the API is missing the
entry list is empty and behaviour is exactly as before.

**Say what that costs, because review did.** The script is `defer`red, and on a
warm cache a browser frequently paints before a deferred script runs — so this
does not only skip the reveal on slow loads, it skips it on **an unknown share
of repeat visits**. The animation is off more often than the sentence above
implies, and that share is not measured: it depends on the browser, the cache
state and the machine, and nothing here samples it. The direction is the safe
one — the failure is a page that appears without animating, never a page that
stays blank — and this is the trade taken knowingly rather than an effect
nobody looked at. Restoring the animation on those visits would mean animating
content the visitor is already reading, which is the flicker this removed.

**And the contract itself is now read by something.** The inversion is three
files and one class name — `.reveal` ships visible in the stylesheet, a rule
scoped to `.js-reveal` is what hides it, the script adds that class, and the
`<noscript>` block puts it back — and nothing in CI opened any of it. Putting
`opacity:0` back on the bare `.reveal` rule, which is the exact state that
shipped a hero and empty space, left every job green. Review named it the next
check to write and the smallest one here, and it was right on both counts.
`check_reveal_contract.py` refuses that state now, and its own mutation tests
run first: `tools/site/test_check_reveal_contract.py` holds 13 cases: 9 demand
a report, 4 demand silence. The first of the nine is the reviewer's own
reproduce step, and two more are the next review's: a selector does not have to
be spelled `.reveal` to hide it — `body .reveal{opacity:0}` beats both the bare
rule and the `<noscript>` override, and `html:not(.js-reveal) .reveal` carries
the scope class and applies exactly when the class is absent. The quiet four
are the ones that would otherwise make this check annoying enough to switch off:
the tree as it stands, a longer transition (that is the animation), a
`transform:none` (not a displacement), and the reduced-motion block setting
`opacity:1` (the accessibility path working).

### `docs/manifest.webmanifest`

Was four keys and a one-line description. Now carries `id`, `scope`, `lang`,
`dir`, `categories` and a full description.

A `maskable` icon purpose was added in the first version of this pass and then
removed, because the claim was not true of the file. A maskable icon has to be
opaque across the whole canvas — the launcher applies its own mask and anything
transparent is a hole. `assets/icon-512.png` is a rounded square on a fully
transparent ground: all four corner pixels read `(0, 0, 0, 0)`. Declaring it
maskable would have had Android inset that rounded square inside its own shape,
which is a worse installed icon than the `any` purpose it already had, not a
better one. The artwork does sit inside the 80 % safe circle, so a genuinely
maskable variant is a matter of adding an opaque background and re-exporting —
a design change, not a manifest one, and not this pull request's subject.

### `docs/sitemap.xml`

One URL with `changefreq` and `priority`, both of which Google documents as
ignored — kept because other crawlers read them and they cost nothing.

**No `lastmod`, and that is a correction rather than an omission.** An earlier
draft of this pass wrote today's date into it with nothing in the repository to
move it, so it would have been wrong from the next commit onwards. Google
honours `lastmod` only where it stays accurate and discounts a feed that does
not — a date nothing maintains is worse than no date. Adding it back means
adding something that updates it first. Found in review; the sitemap says the
same thing in a comment, where the next person will actually be standing.

It is a one-page site; a larger sitemap would be padding.

### `docs/404.html`

Added `noindex, follow`. A soft-404 that indexes is a small, avoidable way to
put a dead page in results under the project's own name.

### `README.md` and `README.ru.md`

The opening sentence is what GitHub's own search indexes and what Google shows
under the repository result. It read *"A wearable firmware platform for ESP32-S3
smartwatches — mesh messaging, offline navigation, and a UI…"*, which contains
almost none of the terms in §1. It now names LoRa, MeshCore, GNSS, LVGL,
FreeRTOS, ESP-IDF, MIT and **both target boards by their full product names** —
which is what someone who owns one of those boards will actually type.

The second paragraph explains the capability registry, because the two-boards-one-codebase
problem is the project's genuinely distinguishing claim and it was buried.

Both files changed in the same commit, per the README pair rule in `CLAUDE.md`.

### `docs/index.html` — hero lead

Strengthened from *"ESP32-S3 wearables: mesh messaging, offline navigation"* to
name LoRa, MeshCore, GNSS, LVGL and FreeRTOS. Every added term is a fact already
claimed elsewhere on the page; nothing new was asserted. The `<h1>` was **not**
touched — see §4.

## 3. What was already correct

Worth recording so nobody "fixes" it:

- **Semantic structure.** Exactly one `<h1>`, a real `<main>`, `<header>`,
  `<footer>`, `<nav aria-label="Primary">`, sectioned content, and a working
  skip-link. This is the part most hand-built project pages get wrong.
- **`docs/robots.txt`** — allows everything and points at the sitemap, and is
  **read by nobody.** `robots.txt` is fetched from the origin root and nowhere
  else. This is a GitHub Pages *project* site with no custom domain — there is
  no `CNAME` in the repository root or in `docs/` — so the file publishes at
  `hleserg.github.io/Attadipa/robots.txt`, while the URL a crawler reads as
  policy is `hleserg.github.io/robots.txt`, which belongs to a user-pages
  repository that does not exist. Every line in it is inert, *including* the
  `Sitemap:` line, so this file is not what makes the sitemap discoverable.
  Nothing else does either: **the sitemap has no automated discovery path
  until §5's console submission is done.** The `<link rel="sitemap">` in the
  head is kept as correct markup, not as coverage — see the note below. The
  file is kept rather than deleted because it becomes live, unchanged, on the
  day a custom domain is added; the header inside it says all of this so the
  next reader does not "fix" the directives when the location is the problem.

  The first version of this section called it *"correct as written"*, which was
  true of the directives and false of the file. Where a file sits is part of
  whether it works.

  **And the sentence that replaced it was unsourced in turn**, which is the
  more useful half of the lesson. Retracting the `robots.txt` claim, this
  document, `docs/robots.txt`, `docs/index.html` and `STATUS.md` all gained
  some form of *"several crawlers read the `<link rel="sitemap">`"* — five
  statements, no crawler named, no citation, and no file in `docs/research/`
  behind them. Review caught it. We then tried to settle it either way and
  **could not**: the network egress proxy in the agent environment blocks
  `developers.google.com` and `www.sitemaps.org`, so neither Google's sitemap
  documentation nor the sitemaps.org protocol could be read. So the claim is
  gone rather than softened, in all five places. If someone later finds a
  crawler that does honour the link, that is a fact for `docs/research/` with
  a citation, and only then does this paragraph change. The rule it broke is
  `CLAUDE.md`'s: a fact that lives only in a chat log does not exist — and a
  section written to stop the next reader re-examining is the worst possible
  place to put one.
- **Canonical URL** present and absolute.
- **Mobile.** A real viewport meta, `img{max-width:100%;height:auto}` in the
  reset, and grid/flex layout throughout — no fixed-width containers to cause a
  horizontal scroll.
- **Performance, otherwise.** One stylesheet under 20 KB and one script under
  12 KB, both local, the script `defer`red. Bounds rather than today's byte
  counts, and deliberately: an exact figure here is held to within half a
  kilobyte, both files sat inside a hundred bytes of their stated size, and any
  CSS rule anyone adds would then turn *Documentation consistency* red on a
  pull request that never opened this file. A check that goes red for a true
  statement gets edited until it stops. What this section claims is that the
  page ships a small amount of first-party code; the bound is that claim, and
  crossing it is a finding rather than a rounding. No web fonts are loaded at all — the type stack
  is `"Nunito Sans", "Avenir Next", system-ui, …`, so there is no render-blocking
  font fetch and no CLS from a swap. No third-party scripts, no analytics, no
  cookie banner.
- **Theme.** `color-scheme: light dark` and a `theme-color` are both declared.

## 4. Deliberately not done, and why

**The `<h1>` still reads "Useful when your phone isn't."** It carries no search
term, and an `<h1>` is a strong signal. It stays because the brief was explicit
that the design is not to be broken, and this line *is* the design — it is the
product's whole argument in four words. The terms were added to the `<title>`,
the lead paragraph and two of the `<h2>`s instead, which is most of the benefit
at none of the cost — the hardware section (`#hardware`; the architecture
section is `#platform` and its `<h2>` was not touched) now opens *"ESP32-S3
boards that share almost nothing: built to survive the differences"* and the status section
*"The foundation is real. The LoRa, GNSS and LVGL firmware on the watch is still
early"*, both in each language. The first version of this section claimed the
`<h2>`s already carried the terms; they did not, and review counted. Naming the
two here is so the next reader can check the claim rather than trust it. Putting keywords in hidden text near the `<h1>` would recover the
rest and is exactly the kind of thing that gets a site penalised; it was not
considered seriously.

**No `hreflang` pair, and this is the site's real SEO limitation.** The head
carries a single `hreflang="x-default"` alternate pointing at the canonical URL
(`index.html:12`) — which says "this URL serves every language" and is honest,
because it does. What it does not carry, and cannot, is an `en`/`ru` pair. The
page is bilingual *inside one document* — English and Russian both live in the
HTML as `.lang-en` / `.lang-ru` spans, with `.lang-ru { display: none }` by
default and a visible toggle switching them. Consequences:

- there is no separate URL for the Russian version, so there is nothing for
  `hreflang` to point at. `?lang=ru` is honoured by the script and, since the
  fix above, does produce a genuinely different rendered head — Russian title,
  Russian description, `og:locale` `ru_RU`. That makes an `hreflang` pair look
  *more* defensible than it did. It was still not added, for one reason: the
  bytes GitHub Pages serves at `?lang=ru` are identical to `/`, and the
  difference exists only after JavaScript has run. Declaring a URL as the
  Russian alternate and having a crawler that did not run the script find
  English there is worse than declaring nothing. The canonical would also have
  to be rewritten per language, in JavaScript, to stop `/` absorbing the
  alternate — which stacks a second run-time-only signal on the first;
- the Russian text is in the DOM but hidden by CSS, and a crawler that does not
  run the toggle will not weigh it. **The Russian content is effectively
  unindexed.** This is a legitimate i18n pattern with a visible control, not
  cloaking — the same markup is served to everyone and the toggle is a real
  button — but it does mean the project cannot rank for a Russian query today.

**And `/` itself now varies by `Accept-Language`, which is new in this pass and
is worth stating plainly.** Before it, `copy.en.title` equalled `copy.ru.title`,
so the rendered head was language-invariant and the canonical URL had one
identity. Now `site.js` runs `setLanguage(initialLanguage())` on every load, and
`browserLanguage()` returns `ru` for any `navigator.languages` entry beginning
`ru-` *or* whose `Intl` region resolves to `RU` — so `en-RU` flips it too. A
Russian-locale renderer on the canonical URL therefore gets a Russian `<title>`,
`description`, `og:title`, `og:description` and `og:locale`, against a canonical
(`index.html:11`) and a lone `x-default` (`:12`) that both say "one version
here".

This is **not cloaking**: the same bytes are served to everyone, the difference
is produced by a script every visitor runs, and the toggle is a visible control.
The exposure is also low — Googlebot crawls from a small set of locales and does
not vary `Accept-Language` per query. But it is a real consequence of making the
head bilingual, it was absent from the first draft of this audit, and it is the
strongest argument for the `/ru/` page below: a separate URL is the only way to
give each language a stable identity that a crawler can attribute.

**The JSON-LD graph stays English under that switch, and that is a decision
rather than an omission.** Review found the asymmetry: `check_head_sync.py`
pairs the meta description with the JSON-LD `WebSite.description` and says an
edit to one is an error on the other, while `setLanguage()` rewrites the meta
tag on every load and rewrites nothing in the graph — so a Russian-locale
visitor gets a Russian meta description beside an English JSON-LD one at the
same URL. Wiring the graph into the switch is the other available fix and was
not taken: the page has one canonical URL, its lone `hreflang` declares that URL
English, and structured data that changes under a client-side toggle would give
one URL two machine-readable identities — the failure the checker exists to
prevent, arriving through the fix for it. The graph therefore describes the
canonical English document, which is what the canonical says it is. The pair is
declared in `RUNTIME_DIVERGENCE` in the checker with that reason, and the
declaration is checked both ways: an undeclared one-sided pair is reported, and
so is a declaration for a pair that has stopped being one-sided — so the day
`/ru/` exists and the graph is wired, this note cannot quietly survive it.

The fix is a genuinely separate `/ru/` page with its own `<title>`,
`description`, canonical and reciprocal `hreflang`. It was not done here because
it means a second 35 KB HTML document to keep in sync by hand, which is the
exact failure mode `CLAUDE.md` already warns about for the README pair — and
duplicating it silently, in a commit whose stated subject is SEO, would be
worse than the problem. **It is an owner decision, and the recommended one.**

**No keyword variants of existing pages, no doorway pages, no near-duplicate
descriptions.** The site is one page because the project is one project.

**`attadipa-banner.png` and `attadipa-style-board.png` are now unreferenced**
and together weigh 3.0 MB in the repository. They are not deleted here: they may
be the source originals the `.webp` files were derived from, and that is not a
call to make inside an SEO commit. If they are not, dropping them takes 3 MB off
every clone.

## 5. Requires a third-party account — not done, listed for the owner

None of these are blockers, and none were registered, per the brief.

| Action | Where | What it buys |
|---|---|---|
| Verify the site in **Google Search Console** | <https://search.google.com/search-console> — DNS or the HTML-file method; the file drops in `docs/` | the only way to see real queries, impressions and Core Web Vitals field data; also where a sitemap is submitted |
| Verify in **Bing Webmaster Tools** | <https://www.bing.com/webmasters> — can import from Search Console in one click | Bing, DuckDuckGo and ChatGPT search all read this index |
| Submit `sitemap.xml` | both consoles | **the only way the sitemap is found at all.** `docs/robots.txt` publishes below the origin root and no crawler reads it (§3), and no crawler is documented as reading the `<link rel="sitemap">` in the head either — so until this is done there is no discovery path, not a weaker one. Also first-crawl latency: hours instead of weeks |
| Check the **Open Graph card** renders | <https://cards-dev.twitter.com/validator> and Facebook's sharing debugger | the OG tags are new and unproven against a real scraper |
| Run **PageSpeed Insights** | <https://pagespeed.web.dev> | confirms the 2.8 MB image saving as a field number rather than an arithmetic one |

## 6. Repository metadata

Already correct at audit time and left alone:

- **Description:** *"Open-source ESP32-S3 smartwatch firmware platform / OS with
  LoRa MeshCore, offline GNSS navigation, LVGL UI and FreeRTOS."*
- **Homepage:** `https://hleserg.github.io/Attadipa/`
- **20 topics**, covering every term in §1.

One observation, and it needs the owner because no API available to an agent here
edits repository topics: **`esp32s3` and `esp32-s3` are both listed.** GitHub
treats them as different topics and does not alias them; `esp32-s3` is the one
with the large repository count behind it. Topics are capped at 20 and the list
is full, so `esp32s3` is most likely a wasted slot. `esp-idf` and `embedded-rust`
are not interchangeable candidates — a better use would be `t-watch`,
`lilygo` or `waveshare`, which are exactly what an owner of one of those boards
searches for and which no current topic covers.

## 7. How to check this did not rot

- `python3 tools/site/check_head_sync.py .` — the one check here that covers the
  files this audit actually changed. In CI, in the *Documentation consistency*
  job, with its own mutation tests ahead of it. It is the answer to the two
  head-drift defects in §2 (*"The head lives in two files"*) recurring.
- `python3 tools/docs/check_docs.py .` — link and structure checks, already in
  CI, and worth knowing the limit of: it filters on `.md`
  (`tools/docs/check_docs.py:89-96`) and never opens `index.html`, `site.js`,
  the manifest or the sitemap. It passed green while the `og:description` defect
  above was in the tree. It guards the prose in this document, not the head it
  describes.
- The JSON-LD is plain JSON inside one `<script>` element: it parses, or it does
  not. Worth a paste into <https://validator.schema.org> after any edit.
- `python3 tools/site/check_site_facts.py .` — the numbers in *this document*
  and in `index.html`, against the files they describe. In CI, in the same
  *Documentation consistency* job. Three of the five `width`/`height` pairs were
  wrong before this pass and the first draft of §"Images" guarded the rest with
  a sentence saying they must be re-read; a sentence is what had just failed, so
  this reads them instead. It measures PNG, JPEG and WebP from their own
  headers and covers the dimension pairs, the byte sizes, both statements of the
  2.8 MB total, the bounds on the stylesheet and the script, and the case count
  quoted above — in this document, in `STATUS.md` and in the CI comment, all
  three read back against what the suite actually ran.
  It compares by proportion, not by scale, so the 64 × 64 `favicon.png` drawn
  at 34 in an `<img>` box is not reported — a check that cried about that would
  teach the reader to skip it. A number it cannot attribute to a file is
  reported, never dropped: a dimension pair naming no file, and a size whose
  sentence names its files on the line above, were both invisible to it until
  the sixth review said so.

  Writing it found two numbers already stale: the script had grown past the
  6 KB stated for it, and the case count above said 29 for a suite of 32. Both
  were changed by commits that had no reason to look at this file, which is the
  argument for the check rather than for more care.
