// Open ?study=02 in a fresh browser page and evaluate this file there.
// Catches a retained confident arrow after data loss, false fix-age wording,
// overlapping/undersized controls and approvals leaking from Study 01.
(async () => {
  const failures = [];
  let checks = 0;
  const assert = (value, message) => {
    checks++;
    if (!value) failures.push(message);
  };
  const select = (selector, value) => {
    const element = document.querySelector(selector);
    element.value = value;
    element.dispatchEvent(new Event("change", { bubbles: true }));
  };
  const scenario = (value) => select('[data-card="nav"]', value);
  // The glance screen's geometry assertions, extracted so a screen that is
  // only walked through can be measured by the same code rather than by a
  // second, weaker copy of it.
  const measure = (screen, context) => {
    const content = screen.querySelector(".content");
    assert(content.scrollHeight <= content.clientHeight + 1, `${context}: no vertical overflow`);
    const bounds = screen.getBoundingClientRect();
    const target = parseFloat(getComputedStyle(screen).getPropertyValue("--target"));
    for (const button of screen.querySelectorAll("button")) {
      const box = button.getBoundingClientRect();
      assert(box.width >= target && box.height >= target, `${context}: full touch target`);
      assert(box.left >= bounds.left && box.right <= bounds.right + 1 && box.bottom <= bounds.bottom + 1, `${context}: control within panel`);
      assert(button.scrollWidth <= button.clientWidth + 1, `${context}: the label fits its button (${button.textContent.trim()})`);
    }
  };
  await document.fonts.ready;
  assert(document.body.dataset.study === "02", "Study 02 is opt-in and visibly identified");
  assert(document.querySelector('#review-nav').dataset.key.startsWith("attadipa-study-02-review-v1/"), "New design does not reuse V2 approval keys");
  for (const size of ["small", "large"]) {
    select("#size", size);
    for (const theme of ["night", "day"]) {
      select("#theme", theme);
      for (const locale of ["ru", "en"]) {
        select("#locale", locale);
        for (const state of ["head-up", "compass-stale", "uncalibrated", "ready", "stale", "no-fix", "node-unknown"]) {
          scenario(state);
          const screen = document.querySelector("#screen-nav");
          const context = `${size}/${theme}/${locale}/${state}`;
          const hasPosition = !["no-fix", "node-unknown"].includes(state);
          assert(!!screen.querySelector(".wrist-arrow") === (state === "head-up"), `${context}: wrist arrow only with usable simulated watch heading`);
          assert(!!screen.querySelector(".bearing-mark") === (hasPosition && state !== "head-up"), `${context}: geographic bearing survives heading loss, but not missing position`);
          assert(screen.textContent.includes(locale === "ru" ? "От часов" : "Watch-relative") === (state === "head-up"), `${context}: visible reference frame`);
          // A cardinal is a fact about north. Under a watch-relative frame it is
          // either wrong or it means the frame label is. The number beside the
          // mode must match the frame the same row names.
          const modeValue = screen.querySelector(".direction-mode span").textContent;
          if (state === "head-up")
            assert(modeValue === "012°", `${context}: watch-relative angle, no cardinal (${modeValue})`);
          else if (hasPosition)
            assert(modeValue === (locale === "ru" ? "057° СВ" : "057° NE"), `${context}: north-up angle keeps its cardinal (${modeValue})`);
          // The number and the drawing are one claim in two notations. A screen
          // that says 012 and points at 057 is worse than one that says neither.
          const mark = screen.querySelector(".wrist-arrow,.bearing-mark");
          if (mark) {
            const drawn = Number(mark.getAttribute("transform").match(/rotate\(\s*(-?[\d.]+)/)[1]);
            assert(drawn === Number(modeValue.slice(0, 3)), `${context}: the mark is drawn at the angle the row states (${drawn} vs ${modeValue})`);
          }
          // ADR-0009 §5: "Position + bearing, standing still" is drawn
          // "north-up, bearing marked, 'walk a few steps to orient'".
          if (state === "ready")
            assert(screen.textContent.includes(locale === "ru" ? "Пройдите несколько шагов" : "Walk a few steps to orient"), `${context}: a north-up screen says why it is north-up`);
          // ADR-0009 §5: Uncalibrated is drawn with "the calibration entry point".
          const calibrate = screen.querySelector('[data-action="nav-calibrate"]');
          assert(!!calibrate === (state === "uncalibrated"), `${context}: calibration is offered exactly where the heading is uncalibrated`);
          if (calibrate) {
            calibrate.click();
            assert(screen.textContent.includes(locale === "ru" ? "восьмёрку" : "figure eight"), `${context}: calibration says what to do`);
            // Walking a screen is not measuring it. Every geometry assertion
            // below this block runs on the glance screen the Back button
            // returns to, so the one screen this study added -- and the only
            // one with a wrapped sentence at --body rather than --caption --
            // was the only one never measured, across 2 sizes x 2 themes x 2
            // locales. It fits today with about 30px of a 218px box to spare;
            // a longer translation would overflow in silence.
            measure(screen, `${context}: calibration`);
            screen.querySelector('[data-action="nav-return"]').click();
            assert(!!screen.querySelector(".nav-study"), `${context}: calibration returns to the condition it was entered from`);
          }
          assert(screen.textContent.includes(locale === "ru" ? "Фиксация не подтверждена" : "Fix unconfirmed") === hasPosition, `${context}: coordinate qualification stays on the glance screen`);
          // ADR-0009 §5: Uncalibrated is drawn with "the value, marked". A
          // screen that drops the value cannot say whether the compass is off
          // by two degrees or by ninety, and the mark alone reads as "no
          // heading" -- which for `compass-stale` is the opposite of true.
          if (["uncalibrated", "compass-stale"].includes(state))
            assert(screen.querySelector(".nav-condition").textContent.includes("045°"), `${context}: the marked heading value is on the glance screen`);
          assert(!screen.textContent.includes(locale === "ru" ? "Готово" : "Ready"), `${context}: no unqualified confident status`);
          measure(screen, `${context}: glance`);
          const bounds = screen.getBoundingClientRect();
          const target = parseFloat(getComputedStyle(screen).getPropertyValue("--target"));
          const items = [...screen.querySelectorAll(".nav-destination,.instrument,.direction-mode,.nav-condition,.actions")];
          for (let i = 1; i < items.length; i++)
            assert(items[i-1].getBoundingClientRect().bottom <= items[i].getBoundingClientRect().top + 1, `${context}: information rows do not overlap`);
          // The row is not the dial: a fixed-size dial overflowed a flex row
          // that had shrunk, and every assertion above passed while it sat on
          // the title and the mode line. Measure the drawing against its row.
          const row = screen.querySelector(".instrument");
          const dial = screen.querySelector(".instrument svg");
          const rowBox = row.getBoundingClientRect();
          const dialBox = dial.getBoundingClientRect();
          assert(dialBox.height > 0 && dialBox.width > 0, `${context}: the dial is drawn`);
          assert(Math.abs(dialBox.width - dialBox.height) <= 1, `${context}: the dial stays circular`);
          assert(dialBox.top >= rowBox.top - 1 && dialBox.bottom <= rowBox.bottom + 1, `${context}: the dial fits the row it is in`);
          // Fitting the row is not filling it. `.screen.small svg` sets 22px for
          // icons and used to tie with the dial's own rule on specificity, so
          // the dial kept its size by source order alone -- and a 22px dial
          // passes every assertion above it, circular, inside its row and with
          // a cardinal fitted to the panel. This is the one that notices.
          assert(dialBox.height >= Math.min(rowBox.height, 180) - 1, `${context}: the dial takes the row it is given (${dialBox.height.toFixed(1)} of ${rowBox.height.toFixed(1)})`);
          // What is painted, not what is declared: stroke-width is in user
          // units, so a 180 viewBox drawn at 80px paints 1.6 as 0.71 -- and
          // getComputedStyle reports 1.6px either way. An SVG client rect
          // carries the geometry box only, so the scale has to be applied by
          // hand, unless the stroke is exempted from it.
          const ring = dial.querySelector("circle");
          const ringStyle = getComputedStyle(ring);
          const scale = dialBox.width / dial.viewBox.baseVal.width;
          const stroke = parseFloat(ringStyle.strokeWidth)
            * (ringStyle.vectorEffect === "non-scaling-stroke" ? 1 : scale);
          assert(stroke >= 1, `${context}: dial strokes are at least a rendered pixel wide (${stroke.toFixed(2)})`);
          // Painted pixels, and both boxes measured the same way. The pair of
          // assertions this replaces worked in the user units `fitDialLabel`
          // computes in and compared the label's `getBBox().y` against
          // `ringTop + band / 2` -- the very expression `place()` had just
          // written, plus exactly the one unit of slack the fix adds. It passed
          // by arithmetic for any font, any `--caption` and any radius, and the
          // only mutation it could ever see was deleting the fix outright.
          const ringBox = ring.getBoundingClientRect();
          // WHAT THAT RECT IS, asserted rather than assumed, because the bound
          // below is read off it and round 4 read it the other way. An SVG
          // client rect carries the GEOMETRY box: measured here it is
          // 2 * r * scale to five decimal places, stroke excluded. So half of
          // it is the band's centreline and the painted inner edge is half a
          // stroke inside that -- not a whole one.
          assert(Math.abs(ringBox.width - 2 * ring.r.baseVal.value * scale) <= 0.01, `${context}: the ring's client rect is its geometry box (${ringBox.width.toFixed(3)} vs ${(2 * ring.r.baseVal.value * scale).toFixed(3)})`);
          const inner = Math.min(ringBox.width, ringBox.height) / 2 - stroke / 2;
          const cx = (ringBox.left + ringBox.right) / 2;
          const cy = (ringBox.top + ringBox.bottom) / 2;
          const offCentre = (box) => Math.hypot((box.left + box.right) / 2 - cx, (box.top + box.bottom) / 2 - cy);
          const label = dial.querySelector("text.cardinal");
          // WHICHEVER MARK THE DIAL HAS, above the split. Narrowing the
          // selector to the cardinal narrowed this with it, from 56 cells to
          // 40, and the 16 it left are the ones where the em-dash *is* the
          // dial: `--dial-label` unset falls back to 16 user units, which is
          // 6.2px at 240x240 against a 12px token. The legibility of the only
          // mark on the screen does not depend on which mark it is.
          const drawn = label || dial.querySelector("text");
          if (drawn) {
            const caption = parseFloat(getComputedStyle(screen).getPropertyValue("--caption"));
            const rendered = parseFloat(getComputedStyle(drawn).fontSize) * scale;
            assert(rendered >= caption - 0.5, `${context}: the dial's mark is no smaller than the panel's smallest type (${rendered.toFixed(1)} vs ${caption})`);
          }
          if (label) {
            // The corners, not the top edge: the dial counter-rotates the
            // cardinal around the ring, so at head-up it rides at 45 degrees
            // and a top-edge comparison is vacuous there. Every corner inside
            // the ring's painted inner edge is the whole claim -- "the ring is
            // not drawn through the letter" -- at every rotation.
            // `getBoundingClientRect` carries the transform `getBBox` ignores.
            const ink = label.getBoundingClientRect();
            const far = Math.max(...[[ink.left, ink.top], [ink.right, ink.top], [ink.left, ink.bottom], [ink.right, ink.bottom]]
              .map(([x, y]) => Math.hypot(x - cx, y - cy)));
            assert(far <= inner, `${context}: the whole cardinal is inside the dial ring (${far.toFixed(2)} of ${inner.toFixed(2)})`);
            assert(offCentre(ink) >= inner / 2, `${context}: the cardinal stays out of the dial's centre (${offCentre(ink).toFixed(2)} of ${(inner / 2).toFixed(2)})`);
          } else {
            // The other half of the same claim, and the half nothing asserted:
            // where there is no direction at all, the dial's one mark belongs
            // at its centre. `fitDialLabel` took "the first text in the dial"
            // and hoisted the em-dash to the top of the ring -- to twelve
            // o'clock, where a bearing due north is drawn -- in all 16 `no-fix`
            // and `node-unknown` cells, and 1914 checks passed.
            const dash = dial.querySelector("text");
            assert(!!dash, `${context}: a dial with no direction still draws the mark that says so`);
            if (dash)
              assert(offCentre(dash.getBoundingClientRect()) <= inner / 2, `${context}: the no-direction mark stays at the dial's centre (${offCentre(dash.getBoundingClientRect()).toFixed(2)} of ${(inner / 2).toFixed(2)})`);
          }
          if (theme === "night")
            assert(getComputedStyle(dial).backgroundColor !== "rgba(0, 0, 0, 0)", `${context}: the dial keeps a ground to be read against`);
          const details = screen.querySelector('[data-action="nav-details"]');
          details?.click();
          assert(screen.textContent.includes(locale === "ru" ? "Симуляция" : "Simulation"), `${context}: details state the evidence boundary`);
          const headingRow = [...screen.querySelectorAll("dd")][1];
          if (["head-up", "uncalibrated", "compass-stale"].includes(state)) {
            assert(headingRow?.textContent.includes("045°"), `${context}: details carry the heading value (${headingRow?.textContent})`);
            assert((headingRow?.textContent.trim() !== "045°") === (state !== "head-up"), `${context}: a heading that is not trustworthy is marked as such (${headingRow?.textContent})`);
          } else {
            assert(headingRow?.textContent.trim() === "—", `${context}: no heading means no number (${headingRow?.textContent})`);
          }
          const detailContent = screen.querySelector(".content");
          assert(detailContent.scrollHeight <= detailContent.clientHeight + 1, `${context}: details do not overflow`);
          const back = screen.querySelector('[data-action="nav-return"]');
          const backBox = back?.getBoundingClientRect();
          assert(backBox && backBox.width >= target && backBox.height >= target && backBox.bottom <= bounds.bottom, `${context}: full Back target inside details`);
          screen.querySelector('[data-action="nav-return"]')?.click();
          assert(document.querySelector('[data-card="nav"]').value === state, `${context}: back preserves the reviewed condition`);
        }
      }
    }
  }
  scenario("head-up");
  const motion = document.querySelector("#motion");
  const setMotion = (on) => {
    motion.checked = on;
    motion.dispatchEvent(new Event("change", {bubbles: true}));
  };
  const animations = (selector) =>
    [...document.querySelectorAll(selector)].reduce((n, e) => n + e.getAnimations({subtree: true}).length, 0);
  // Positive control first: without it "no animation" passes on a toggle that
  // governs nothing, which is what it did while nothing animated the compass.
  setMotion(true);
  const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (!reduced)
    assert(animations("#screen-nav .fireflies i") > 0, "Motion-on animates what the toggle governs");
  assert(animations("#screen-nav .wrist-arrow") === 0, "The compass is still in both motion states, not animated in one");
  setMotion(false);
  assert(animations("#screen-nav .fireflies i") === 0, "Motion-off stops it");
  assert(animations("#screen-nav .wrist-arrow") === 0, "Motion-off has no animated compass");
  setMotion(true);
  return {passed: failures.length === 0, checks, failures};
})();
