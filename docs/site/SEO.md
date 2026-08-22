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
meta tag ends *"Early implementation, not yet run on hardware"* on purpose. It
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
| `description` 178 chars, ended on "product-grade UI" | 151 chars, names LoRa MeshCore / GNSS / LVGL / FreeRTOS and closes on the early-implementation caveat |
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

### `docs/manifest.webmanifest`

Was four keys and a one-line description. Now carries `id`, `scope`, `lang`,
`dir`, `categories`, a full description, and a `maskable` icon purpose so an
installed icon is not letterboxed on Android.

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
- **Performance, otherwise.** One 17 KB stylesheet and one 3 KB script, both
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

**No `hreflang` alternates, and this is the site's real SEO limitation.** The
page is bilingual *inside one document* — English and Russian both live in the
HTML as `.lang-en` / `.lang-ru` spans, with `.lang-ru { display: none }` by
default and a visible toggle switching them. Consequences:

- there is no separate URL for the Russian version, so there is nothing for
  `hreflang` to point at. `?lang=ru` is honoured by the script but serves
  byte-identical HTML, so declaring it an alternate would announce duplicate
  content rather than a translation;
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

- `python3 tools/docs/check_docs.py .` — link and structure checks over the docs
  tree, already in CI.
- The JSON-LD is plain JSON inside one `<script>` element: it parses, or it does
  not. Worth a paste into <https://validator.schema.org> after any edit.
- If an image is swapped, its `width`/`height` must be re-read from the file.
  Three of the five were wrong before this pass, which is what happens when the
  numbers are typed rather than measured.
