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
    const hint = screen.querySelector(".empty .sub");
    if (hint)
      assert(
        getComputedStyle(hint).backgroundColor ===
          getComputedStyle(screen).backgroundColor,
        `${context}: the state instruction has its token reading surface`,
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
  assert(
    [...document.fonts].some(
      (font) => font.family.includes("Nunito") && font.status === "loaded",
    ),
    "The pinned Nunito font must actually load",
  );
  for (const source of [
    "glade-day-v2.png",
    "glade-night-v2.png",
    "../../../ui/assets/source/backgrounds/clock_meadow_night_410x502.png",
  ]) {
    const image = new Image();
    image.src = source;
    try {
      await image.decode();
    } catch {
      /* Report a broken raster below. */
    }
    assert(image.naturalWidth > 0, `Background loads: ${source}`);
  }
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
        scenario("mesh", "ready");
        click("mesh", "mesh-details");
        assert(
          text("mesh").includes(locale === "ru" ? "7,25 дБ" : "7.25 dB"),
          "Mesh details uses the selected locale",
        );
        fit("mesh", `${size}/${theme}/${locale}/mesh-details`);
        scenario("setup", "failed");
        assert(
          !document.querySelector("#screen-setup .lumar") &&
            document.querySelector("#screen-setup .failure-symbol"),
          "Failure has warning art, not success art",
        );
        scenario("setup", "code-saved");
        assert(
          text("setup").includes(
            locale === "ru" ? "Код сохранён" : "Passkey saved",
          ),
          "Passkey receipt is selectable in the gallery",
        );
        scenario("nav", "stale");
        click("nav", "nav-details");
        assert(
          text("nav").includes(locale === "ru" ? "2 мин" : "2 min"),
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
  click("setup", "menu");
  click("setup", "time");
  assert(
    text("setup").includes("01.02.2026"),
    "Re-entry discards an abandoned date draft",
  );
  scenario("mesh", "absent");
  click("mesh", "passkey");
  click("mesh", "previous-code");
  assert(
    text("mesh").includes("No node yet") && !text("mesh").includes("4c9a2f7b"),
    "Passkey Back preserves absent-node entry context",
  );
  scenario("setup", "forgotten");
  click("setup", "passkey");
  click("setup", "previous-code");
  assert(
    text("setup").includes("Node forgotten") &&
      !text("setup").includes("4c9a2f7b"),
    "Passkey Back does not resurrect a forgotten node",
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
  // Preserve any real feedback: the check only owns the temporary edits below.
  const originalReviews = new Map(reviews);
  const originalUnreadable = new Map(unreadableReviews);
  const originalStorage = new Map(
    [...reviews.keys(), ...unreadableReviews.keys()].map((key) => [
      key,
      localStorage.getItem(key),
    ]),
  );
  const originalStorageFailed = reviewStorageFailed;
  const setItem = Storage.prototype.setItem;
  try {
    select("#size", "large");
    select("#theme", "night");
    scenario("clock", "ready");
    const key = document.querySelector("#review-clock").dataset.key;
    const note = "More breathing room. <b>Literal text, not markup.</b>";
    const input = document.querySelector("#note-clock");
    input.value = note;
    input.dispatchEvent(new Event("input", { bubbles: true }));
    select("#decision-clock", "changes");
    assert(
      JSON.parse(localStorage.getItem(key)).note === note,
      "Review note is persisted",
    );
    assert(
      JSON.parse(localStorage.getItem(key)).status === "changes",
      "Decision is persisted",
    );
    select("#theme", "day");
    assert(
      document.querySelector("#review-clock").dataset.key !== key,
      "Themes have distinct review scopes",
    );
    select("#theme", "night");
    assert(
      input.value === note &&
        document.querySelector("#decision-clock").value === "changes",
      "Returning to a variant restores its review",
    );
    assert(
      !document.querySelector("#review-clock b"),
      "Review text is never rendered as HTML",
    );
    click("clock", "menu");
    assert(
      document.querySelector("#review-clock").dataset.key !== key,
      "An interaction changes review scope",
    );
    scenario("clock", "ready");
    assert(
      exportReview().includes(note) &&
        exportReview().includes("Decision: changes"),
      "Export contains note, decision and context",
    );
    const navKey = document.querySelector("#review-nav").dataset.key;
    const child = document.querySelector("#child");
    child.checked = true;
    child.dispatchEvent(new Event("change"));
    assert(
      document.querySelector("#review-nav").dataset.key === navKey,
      "Child clock does not split non-clock reviews",
    );
    assert(
      document.querySelector("#review-clock").dataset.key !== key,
      "Child clock has its own review",
    );
    child.checked = false;
    child.dispatchEvent(new Event("change"));
    assert(
      !document.querySelector("#export-review").disabled,
      "A review enables download",
    );
    const anchorClick = HTMLAnchorElement.prototype.click;
    let downloaded;
    try {
      HTMLAnchorElement.prototype.click = function () {
        assert(
          this.download === "attadipa-design-review.txt",
          "Download has a portable filename",
        );
        downloaded = fetch(this.href).then((response) => response.text());
      };
      document.querySelector("#export-review").click();
      assert(
        (await downloaded).includes(note),
        "The real download button exports the note",
      );
    } finally {
      HTMLAnchorElement.prototype.click = anchorClick;
    }
    Storage.prototype.setItem = () => {
      throw new Error("Test storage unavailable");
    };
    input.value = "Unsaved browser note remains exportable";
    input.dispatchEvent(new Event("input", { bubbles: true }));
    assert(
      document
        .querySelector("#review-notice")
        .textContent.includes("Browser storage failed"),
      "Storage failure is visible",
    );
    assert(
      exportReview().includes(input.value),
      "Storage failure does not lose the in-memory export",
    );
    Storage.prototype.setItem = setItem;
    const invalidKey = reviewPrefix + "selftest-invalid";
    const validKey = reviewPrefix + "selftest-valid";
    const legacyKey = "attadipa-study-01-review-v1/selftest-legacy";
    const validEntry = {
      context: "selftest only",
      status: "changes",
      note: "Recovered valid note",
      preview: "Clock",
    };
    localStorage.setItem(invalidKey, "{broken-json");
    localStorage.setItem(validKey, JSON.stringify(validEntry));
    localStorage.setItem(
      legacyKey,
      JSON.stringify({ ...validEntry, note: "Earlier V1 feedback" }),
    );
    reviews.clear();
    unreadableReviews.clear();
    loadReviews();
    assert(
      reviews.get(validKey)?.note === validEntry.note,
      "One corrupt entry cannot hide valid reviews",
    );
    assert(
      exportReview().includes("{broken-json"),
      "Unreadable records remain recoverable in export",
    );
    assert(
      exportReview().includes("Earlier V1 feedback") &&
        exportReview().includes("V1 (previous design)"),
      "V1 feedback remains exported with its revision",
    );
  } finally {
    Storage.prototype.setItem = setItem;
    for (const key of [...reviews.keys(), ...unreadableReviews.keys()]) {
      if (!originalStorage.has(key)) localStorage.removeItem(key);
    }
    reviews.clear();
    unreadableReviews.clear();
    for (const [key, entry] of originalReviews) reviews.set(key, entry);
    for (const [key, raw] of originalUnreadable)
      unreadableReviews.set(key, raw);
    for (const [key, stored] of originalStorage) {
      if (stored === null) localStorage.removeItem(key);
      else localStorage.setItem(key, stored);
    }
    reviewStorageFailed = originalStorageFailed;
    render();
  }
  const motion = document.querySelector("#motion");
  const originalMotion = motion.checked;
  scenario("clock", "ready");
  motion.checked = false;
  motion.dispatchEvent(new Event("change"));
  assert(
    !document.querySelector(".fireflies"),
    "Motion off removes decorative animation",
  );
  motion.checked = true;
  motion.dispatchEvent(new Event("change"));
  const particles = document.querySelectorAll("#screen-clock .fireflies i");
  assert(
    particles.length === (reducedMotion.matches ? 0 : 3),
    "Motion respects the system preference",
  );
  if (particles.length) {
    click("clock", "nav");
    const clockScreen = document.querySelector("#screen-clock");
    const particle = clockScreen.querySelector(".fireflies i");
    assert(
      clockScreen.contains(document.activeElement),
      "Navigation retains keyboard focus without needing a click outside",
    );
    const before = getComputedStyle(particle).transform;
    await new Promise((resolve) => setTimeout(resolve, 350));
    assert(
      getComputedStyle(particle).animationPlayState === "running" &&
        getComputedStyle(particle).transform !== before,
      "Fireflies resume after navigation while focus stays on the screen",
    );
    clockScreen.dataset.visible = "false";
    assert(
      getComputedStyle(particle).animationPlayState === "paused",
      "Offscreen decorative motion is paused",
    );
    delete clockScreen.dataset.visible;
    document.body.classList.add("motion-paused");
    assert(
      getComputedStyle(particle).animationPlayState === "paused",
      "Hidden-page motion policy pauses animations",
    );
    document.body.classList.remove("motion-paused");
  }
  motion.checked = originalMotion;
  motion.dispatchEvent(new Event("change"));
  const result = { checks, failures, passed: failures.length === 0 };
  window.designCheck = result;
  return result;
})();
