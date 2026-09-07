// Run in the review page's browser context; exercises its actual DOM controls.
(async () => {
  const failures = [];
  let checks = 0;
  const assert = (condition, message) => {
    checks++;
    if (!condition) failures.push(message);
  };
  const select = (selector, value) => {
    const element = document.querySelector(selector);
    element.value = value;
    element.dispatchEvent(new Event("change", { bubbles: true }));
  };
  const click = (id, command) => {
    const button = document.querySelector(
      `#screen-${id} [data-action="${command}"]`,
    );
    if (!button) throw new Error(`${id}: missing ${command}`);
    button.focus();
    button.click();
  };
  const text = (id) => document.querySelector(`#screen-${id}`).textContent;
  const scenario = (id, value) => select(`[data-card="${id}"]`, value);
  const fit = (id, context) => {
    const screen = document.querySelector(`#screen-${id}`);
    const content = screen.querySelector(".content");
    const bounds = screen.getBoundingClientRect();
    const target = parseFloat(
      getComputedStyle(screen).getPropertyValue("--target"),
    );
    assert(
      content.scrollHeight <= content.clientHeight + 1,
      `${context}: vertical overflow ${content.scrollHeight}/${content.clientHeight}`,
    );
    for (const button of screen.querySelectorAll("button:not(:disabled)")) {
      const rect = button.getBoundingClientRect();
      assert(
        rect.width >= target - 1 && rect.height >= target - 1,
        `${context}: undersized ${button.textContent}`,
      );
      assert(
        rect.left >= bounds.left &&
          rect.right <= bounds.right + 1 &&
          rect.top >= bounds.top &&
          rect.bottom <= bounds.bottom + 1,
        `${context}: escaped button ${button.textContent}`,
      );
    }
  };
  await document.fonts.ready;
  for (const size of ["small", "large"]) {
    select("#size", size);
    for (const theme of ["day", "night"]) {
      select("#theme", theme);
      for (const locale of ["en", "ru"]) {
        select("#locale", locale);
        for (const control of document.querySelectorAll("[data-card]")) {
          for (const option of control.options) {
            select(`[data-card="${control.dataset.card}"]`, option.value);
            fit(
              control.dataset.card,
              `${size}/${theme}/${locale}/${control.dataset.card}/${option.value}`,
            );
          }
        }
        scenario("setup", "time");
        for (let step = 0; step < 6; step++) {
          fit("setup", `${size}/${theme}/${locale}/time/${step}`);
          click("setup", "next");
        }
        fit("setup", `${size}/${theme}/${locale}/review`);
        scenario("mesh", "ready");
        click("mesh", "message");
        fit("mesh", `${size}/${theme}/${locale}/message`);
        scenario("nav", "stale");
        click("nav", "nav-details");
        assert(
          text("nav").includes("2 min"),
          "Stale age must survive entering details",
        );
        fit("nav", `${size}/${theme}/${locale}/nav-details`);
        click("nav", "nav");
        assert(
          text("nav").includes(locale === "ru" ? "2 мин" : "2 min"),
          "Stale state must survive Back",
        );
        scenario("setup", "node");
        click("setup", "confirm-forget");
        fit("setup", `${size}/${theme}/${locale}/confirm-forget`);
        click("setup", "node");
        assert(
          text("setup").includes("4c9a2f7b"),
          "Cancel forget must retain node",
        );
      }
    }
  }
  select("#locale", "en");
  scenario("setup", "time");
  // Choose 31 January, then February: the actual controls must clamp the day.
  for (let i = 0; i < 31; i++) click("setup", "plus");
  click("setup", "next");
  for (let i = 0; i < 12; i++) click("setup", "minus");
  click("setup", "previous");
  for (let i = 0; i < 31; i++) click("setup", "plus");
  assert(text("setup").includes("31.01.2026"), "Can select 31 January");
  click("setup", "next");
  click("setup", "plus");
  assert(
    text("setup").includes("28.02.2026"),
    "Changing to February clamps day",
  );
  // Choose day 1, local 00:00, UTC+03:00: UTC must cross into January.
  click("setup", "previous");
  for (let i = 0; i < 31; i++) click("setup", "minus");
  for (let i = 0; i < 3; i++) click("setup", "next");
  for (let i = 0; i < 24; i++) click("setup", "minus");
  click("setup", "next");
  for (let i = 0; i < 60; i++) click("setup", "minus");
  click("setup", "next");
  click("setup", "next");
  assert(
    text("setup").includes("2026-01-31 21:00"),
    "UTC conversion crosses date boundary",
  );
  click("setup", "save-time");
  assert(
    text("setup").includes("Time is set"),
    "Save reaches the time receipt",
  );
  click("setup", "home");
  assert(
    text("setup").includes("00:00"),
    "Clock shows the time that was saved",
  );
  click("setup", "menu");
  click("setup", "time");
  click("setup", "plus");
  assert(
    document.activeElement.dataset.action === "plus",
    "Stepper preserves keyboard focus",
  );
  click("setup", "previous");
  click("setup", "home");
  assert(
    text("setup").includes("00:00"),
    "Discarding a draft preserves saved time",
  );
  scenario("setup", "passkey");
  click("setup", "minus-code");
  assert(
    text("setup").includes("900000"),
    "Digit decrements from zero to nine",
  );
  for (let i = 0; i < 6; i++) click("setup", "next-code");
  assert(
    text("setup").includes("Saving passkey") &&
      !text("setup").includes("Passkey saved"),
    "Pending is not success",
  );
  click("setup", "home");
  await new Promise((resolve) => setTimeout(resolve, 1600));
  assert(
    text("setup").includes("Set time"),
    "A late completion must not replace the screen after Close",
  );
  scenario("setup", "passkey");
  for (let i = 0; i < 6; i++) click("setup", "next-code");
  await new Promise((resolve) => setTimeout(resolve, 1600));
  assert(
    text("setup").includes("Passkey saved") &&
      text("setup").includes("Connection is still being checked"),
    "Stored code is not a connected node",
  );
  // Child mode only exists for the clock in this design scope.
  scenario("clock", "ready");
  document.querySelector("#child").checked = true;
  document.querySelector("#child").dispatchEvent(new Event("change"));
  for (const size of ["small", "large"]) {
    select("#size", size);
    fit("clock", `${size}/child`);
    for (const button of document.querySelectorAll(
      "#screen-clock button:not(:disabled)",
    )) {
      assert(
        button.offsetHeight >= (size === "small" ? 77 : 110),
        "Child clock uses child touch targets",
      );
    }
  }
  document.querySelector("#child").checked = false;
  document.querySelector("#child").dispatchEvent(new Event("change"));
  const result = { checks, failures, passed: failures.length === 0 };
  window.designCheck = result;
  return result;
})();
