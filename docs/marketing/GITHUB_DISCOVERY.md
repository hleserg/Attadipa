# Being found, and being understood in twenty seconds

The README is the door. This file is everything *around* the door: what GitHub
shows before a visitor arrives, what a link preview looks like when somebody
shares the project, which pictures are worth taking next, and where it is worth
showing the project once the door is presentable.

Nothing here is applied automatically. The description, the topics and the
social preview are repository settings the owner changes; this file is the
proposal and the reasoning, so the next person does not have to re-derive it.

---

## 1. Repository description

**Current** (2026-09-07):

> Open-source ESP32-S3 smartwatch firmware platform / OS with LoRa MeshCore,
> offline GNSS navigation, LVGL UI and FreeRTOS.

It is a keyword list, and one of the keywords is a claim the project refuses.
`README.md` says in as many words that Atta-dipa **is not** a Linux-like OS, so
"/ OS" in the description contradicts the front page and sets an expectation the
repository then spends a paragraph walking back.

**Proposed** (159 characters, well inside GitHub's 350):

> Open-source ESP32-S3 smartwatch for LoRa mesh and offline navigation — no
> phone, no cloud, no subscription. Every position carries its source, age and
> confidence.

Why this shape: the first sentence is the product and carries the four terms
somebody would actually search (`ESP32-S3`, `smartwatch`, `LoRa mesh`, `offline
navigation`). The second is the differentiator, and it is the one sentence in
the project that no competing repository can copy without doing the work.

If a shorter line is wanted for a place that truncates, cut the second sentence
rather than compressing both.

## 2. GitHub topics

**GitHub caps a repository at 20 topics, and the repository is at 20.** So this
is a swap list, not an addition list.

**Currently set:** `embedded`, `embedded-systems`, `esp-idf`, `esp32`,
`esp32-s3`, `esp32s3`, `firmware`, `freertos`, `gnss`, `gps`, `lora`, `lvgl`,
`mesh-networking`, `meshcore`, `offline-navigation`, `open-source`, `smartwatch`,
`smartwatch-os`, `wearable`, `wearable-tech`.

**Drop five, and why:**

| Topic | Why it is not earning its slot |
|---|---|
| `esp32s3` | duplicate of `esp32-s3`; GitHub does not merge them, and browsers of one see the other's repos anyway through `esp32` |
| `embedded` | `embedded-systems` is the topic with the followers; `embedded` is a near-empty alias |
| `wearable-tech` | same relationship to `wearable` |
| `open-source` | every public repository is; it selects nobody |
| `smartwatch-os` | the README explicitly denies being an OS. A topic that contradicts the front page costs trust from exactly the reader who checks |

**Add five, and why:**

| Topic | Who it reaches |
|---|---|
| `offline-first` | an active community that is mostly web, and for whom a wearable that means it is genuinely novel |
| `open-hardware` | the crowd that reads schematics for fun — the same people who can close an `OPEN_QUESTIONS` row |
| `ble` | the MeshCore link is BLE; people searching BLE + ESP32 are the ones who can help with the transport |
| `u-blox` | narrow and high-intent: the MIA-M10Q is a u-blox part and the GNSS work is real |
| `cpp` | the language filter a contributor actually uses when browsing embedded repos |

**Resulting set (20):** `ble`, `cpp`, `embedded-systems`, `esp-idf`, `esp32`,
`esp32-s3`, `firmware`, `freertos`, `gnss`, `gps`, `lora`, `lvgl`,
`mesh-networking`, `meshcore`, `offline-first`, `offline-navigation`,
`open-hardware`, `smartwatch`, `u-blox`, `wearable`.

Apply with:

```sh
gh repo edit hleserg/Attadipa \
  --remove-topic esp32s3 --remove-topic embedded --remove-topic wearable-tech \
  --remove-topic open-source --remove-topic smartwatch-os \
  --add-topic ble --add-topic cpp --add-topic offline-first \
  --add-topic open-hardware --add-topic u-blox
```

## 3. Social preview

**What is set now:** nothing. `gh repo view` reports the default
`opengraph.githubassets.com` card, which is GitHub's generated one — repository
name, owner avatar, description, and the star/fork counts rendered at a size
where "2 stars" is the most legible thing on it. Every share of this project
currently leads with its own star count.

**What to upload** (Settings → General → Social preview, 1280 × 640):

The project already has a card built for this and it is not being used —
[`docs/assets/og-card.jpg`](../assets/og-card.jpg), already referenced by
`docs/index.html` as the project page's `og:image`. Uploading it makes the
repository and the project page share one identity, which is worth more than a
marginally better bespoke image.

**If a new card is made instead**, the strongest version is the one that shows
the product rather than the brand:

- Left two-thirds: the clock face photographed *on the wrist*, outdoors, at
  dusk — the one shot the repository does not have (see §4).
- Right third: the wordmark, `INDEPENDENT BY DESIGN`, and one line —
  *"LoRa mesh · offline navigation · no phone, no cloud"*.
- Background: the banner's cream and sage, so a shared link and the README
  read as the same project.
- No star count, no badges, no screenshots of code. At Slack and Twitter
  thumbnail sizes only two elements survive; make them the device and the name.

## 4. The five pictures worth taking

The repository's visual evidence is honest and thin: a boot GIF, a device
framebuffer capture of the clock, and a set of research screenshots. Everything
below is a **real photograph or capture the owner can produce**, ranked by what
it unblocks. No rendering, no mockups, no generated devices — the whole value of
this repository's evidence model is that its pictures are of things that exist.

1. **The watch on a wrist, outdoors, showing the clock.** This is the single
   highest-value image and its absence is why the README opens with a
   framebuffer capture instead of a product shot. A framebuffer proves the
   pixels; a wrist proves the object. Dusk or overcast, so the AMOLED is not
   washed out. Becomes the hero image and the social preview.

2. **The navigation screen showing a real distance and bearing to a real node.**
   The moment [#450](https://github.com/hleserg/Attadipa/issues/450) produces
   `NODE / 742 m / ↗ NE` against a companion that is genuinely 742 m away, that
   frame is the proof of the entire product thesis. Capture it the same day it
   first happens, with the console log beside it.

3. **A side-by-side of the two topologies as objects on a table.** T-Watch alone
   on the left; Waveshare plus the companion node on the right, with the BLE
   link implied by their placement. The ASCII diagram in the README explains it;
   a photograph makes a stranger understand it without reading.

4. **The simulator window and the physical watch showing the same screen, in one
   frame.** This is the argument for the whole development setup, and it takes
   one laptop, one watch and one phone camera. It is also the picture that
   recruits UI contributors, because it says *"you can work on this today,
   without buying anything."*

5. **The magnetometer retrofit, mid-solder.** An opened watch, the module, the
   four wires, the flux. Hardware people trust a project that has opened the
   case, and this is the picture that says the project does its own work rather
   than describing it. Take it whether or not the retrofit succeeds — a failed
   one is still evidence, and this repository publishes failures.

Two more, cheap and worth having: a short GIF of the boot-to-clock sequence on
the **T-Watch** (the repository only has the Waveshare one), and the mesh screen
photographed while a message actually arrives, rather than screenshotted after.

## 5. Where to show it, and what to show each place

One post for all of them is the mistake. Each of these communities cares about a
different thing, and three of the five will actively resent a generic
announcement. **Post after the README lands, not before** — the whole point is
that the door is now presentable.

| Where | What to lead with | What not to lead with |
|---|---|---|
| **r/esp32** and the ESP32 forum | The two-boards-one-binary capability layer, and the fact that `apps/` cannot link against the hardware layer at all. This audience has all written the `#ifdef BOARD_X` that this project refuses, and the enforcement mechanism is the interesting part | the mesh; they have seen a dozen |
| **MeshCore community** (Discord / the upstream repo's Discussions) | A wearable MeshCore *client* that stays compatible with upstream rather than forking — plus the honest gap: pairing and receive are `MEASURED`, send-and-see-the-reply is `NOT OBSERVED`. Ask for help closing it | "we built a MeshCore watch" while the reply path is unproven. This community will check |
| **LVGL forum and Discord** | The design-token system with WCAG contrast arithmetic, the CI check that rejects a raw hex value in screen code, and the two-geometry-one-binary problem. Show the clock face | the hardware; they want the UI story |
| **r/LoRa and the meshtastic-adjacent crowd** | The split topology: a wrist terminal with no radio talking to a node that has one, and *why* the coordinate is not allowed to be promoted to "your position". That is a design argument this community argues about constantly | positioning it as a Meshtastic competitor. It is not one, and saying so early prevents the whole thread being about that |
| **Hackaday tip line / r/openhardware** | The evidence discipline: `UNKNOWN` written down instead of guessed, both schematics read sheet by sheet, a conflict between vendor doc and schematic recorded as a conflict, and one measured power number instead of a datasheet one. This is a *story*, and it is rarer than the hardware | the feature list. Hackaday runs process stories about firmware all the time and feature lists almost never |

A sixth, only when picture 2 from §4 exists: **r/EDC and the offline-preparedness
communities**. They are the end users rather than the builders, and they should
be shown the working thing, never the architecture.

**What not to do.** No mass cross-posting on one day, no "check out my project"
with a bare link, and nothing at all posted to a community whose specific
question the README cannot answer yet. The repository's credibility is built on
not overclaiming; a launch post that overclaims spends it in one afternoon.

## 6. What this file does not cover

`docs/index.html` — the GitHub Pages project page — carries its own copy of the
pitch, and it was not reviewed as part of this change. It should be read against
the new README before the next round of promotion, because the two are now the
project's two front doors and a visitor may meet either first.
