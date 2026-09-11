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
            screen.querySelector('[data-action="nav-return"]').click();
            assert(!!screen.querySelector(".nav-study"), `${context}: calibration returns to the condition it was entered from`);
          }
          assert(screen.textContent.includes(locale === "ru" ? "Фиксация не подтверждена" : "Fix unconfirmed") === hasPosition, `${context}: coordinate qualification stays on the glance screen`);
          assert(!screen.textContent.includes(locale === "ru" ? "Готово" : "Ready"), `${context}: no unqualified confident status`);
          const content = screen.querySelector(".content");
          assert(content.scrollHeight <= content.clientHeight + 1, `${context}: no vertical overflow`);
          const bounds = screen.getBoundingClientRect();
          const target = parseFloat(getComputedStyle(screen).getPropertyValue("--target"));
          for (const button of screen.querySelectorAll("button")) {
            const box = button.getBoundingClientRect();
            assert(box.width >= target && box.height >= target, `${context}: full touch target`);
            assert(box.left >= bounds.left && box.right <= bounds.right + 1 && box.bottom <= bounds.bottom + 1, `${context}: control within panel`);
          }
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
          const label = dial.querySelector("text");
          if (label) {
            const caption = parseFloat(getComputedStyle(screen).getPropertyValue("--caption"));
            const rendered = parseFloat(getComputedStyle(label).fontSize) * scale;
            assert(rendered >= caption - 0.5, `${context}: the cardinal is no smaller than the panel's smallest type (${rendered.toFixed(1)} vs ${caption})`);
          }
          if (theme === "night")
            assert(getComputedStyle(dial).backgroundColor !== "rgba(0, 0, 0, 0)", `${context}: the dial keeps a ground to be read against`);
          const details = screen.querySelector('[data-action="nav-details"]');
          details?.click();
          assert(screen.textContent.includes(locale === "ru" ? "Симуляция" : "Simulation"), `${context}: details state the evidence boundary`);
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
