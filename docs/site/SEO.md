# SEO — what was audited, what changed, and what is deliberately not done

Audited 2026-08-23 against `docs/` (the GitHub Pages site at
<https://hleserg.github.io/Attadipa/>), `README.md`, `README.ru.md` and the
repository's own metadata. This file is the record: what the state was, what
changed, and — the part that matters more — the claims that were **not** made
because they are not true yet.

## 0. The constraint this whole document works under

Attadipa is at early implementation. Six libraries and a simulator build and
pass 24 host tests; **no Attadipa firmware has run on a physical board**, and no
power, timing or GNSS number has been measured. Every line below was written so
that a reader arriving from a search engine learns that in the first screenful
rather than the fifth.

Concretely, that ruled out: "the best smartwatch OS", any superlative, any
feature described as working that has only been designed, any benchmark, and any
metric — stars, downloads, users — dressed up as adoption. The `description`
meta tag ends *"Early stage — not yet run on hardware."* on purpose, and
`og:description` ends *"Early implementation — no board has run it yet."* It
costs click-through and it is the correct trade.

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

Three `width`/`height` pairs were also wrong, which is a layout-shift source
rather than a cosmetic error — the browser reserves the declared box and then
reflows when the real image arrives:

- banner declared 1788 × 894, is 1774 × 887;
- design board declared 1440 × 1086, is 1448 × 1086;
- mascot declared 720 × 708, is 1270 × 1239.

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
the *Documentation consistency* job. It extracts the English `<title>` and six
`<meta>` contents from `index.html`, the same six fields from `site.js`'s
`copy.en`, and fails naming the field and both values on any divergence; the two
`twitter:` tags are checked against the `og:` strings they mirror, and `copy.ru`
is checked for presence, since the Russian strings have no counterpart in the
HTML by construction. Its own mutation tests
(`tools/site/test_check_head_sync.py`, twenty cases) run first and include one
per historical defect, because a checker that passes everything is worse than
none — it is what the next agent trusts instead of re-checking, which is exactly
how this section came to be wrong.

### `docs/index.html` — a `<noscript>` fallback for `.reveal`

Not an SEO fix, found while tracing what the script does to the head. Every
section below the hero carries `.reveal`, which ships at `opacity:0`
(`site.css:8`); `site.js` adds `.visible` through an `IntersectionObserver` as
each scrolls into view. With scripting off nothing ever adds it, so a document
that contains the whole page renders as a hero and empty space. Googlebot runs
scripts and never saw this; a reader with scripting off, or one whose script
request simply failed, saw a blank page.

A `<noscript>` block now restores `opacity:1` and clears the transform. It
changes nothing for anyone running the script, and the reduced-motion media
query already did the same thing for people who ask for it.

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

One URL, now with `lastmod`, `changefreq` and `priority`. It is a one-page site;
a larger sitemap would be padding.

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
- **`docs/robots.txt`** — allows everything and points at the sitemap. Correct
  as written.
- **Canonical URL** present and absolute.
- **Mobile.** A real viewport meta, `img{max-width:100%;height:auto}` in the
  reset, and grid/flex layout throughout — no fixed-width containers to cause a
  horizontal scroll.
- **Performance, otherwise.** One 17 KB stylesheet and one 6 KB script, both
  local, the script `defer`red. No web fonts are loaded at all — the type stack
  is `"Nunito Sans", "Avenir Next", system-ui, …`, so there is no render-blocking
  font fetch and no CLS from a swap. No third-party scripts, no analytics, no
  cookie banner.
- **Theme.** `color-scheme: light dark` and a `theme-color` are both declared.

## 4. Deliberately not done, and why

**The `<h1>` still reads "Useful when your phone isn't."** It carries no search
term, and an `<h1>` is a strong signal. It stays because the brief was explicit
that the design is not to be broken, and this line *is* the design — it is the
product's whole argument in four words. The terms were added to the `<title>`,
the lead paragraph and the `<h2>`s instead, which is most of the benefit at none
of the cost. Putting keywords in hidden text near the `<h1>` would recover the
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
| Submit `sitemap.xml` | both consoles | first-crawl latency, hours instead of weeks |
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
  head-drift defects in §3 recurring.
- `python3 tools/docs/check_docs.py .` — link and structure checks, already in
  CI, and worth knowing the limit of: it filters on `.md`
  (`tools/docs/check_docs.py:89-96`) and never opens `index.html`, `site.js`,
  the manifest or the sitemap. It passed green while the `og:description` defect
  above was in the tree. It guards the prose in this document, not the head it
  describes.
- The JSON-LD is plain JSON inside one `<script>` element: it parses, or it does
  not. Worth a paste into <https://validator.schema.org> after any edit.
- If an image is swapped, its `width`/`height` must be re-read from the file.
  Three of the five were wrong before this pass, which is what happens when the
  numbers are typed rather than measured.
