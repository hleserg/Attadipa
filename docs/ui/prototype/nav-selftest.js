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
  motion.checked = false;
  motion.dispatchEvent(new Event("change", {bubbles: true}));
  assert(document.querySelector("#screen-nav .wrist-arrow")?.getAnimations({subtree: true}).length === 0, "Motion-off has no animated compass");
  return {passed: failures.length === 0, checks, failures};
})();
