(() => {
  const html = document.documentElement;
  const buttons = [...document.querySelectorAll('[data-set-lang]')];
  const metaDescription = document.querySelector('meta[name="description"]');
  const ogDescription = document.querySelector('meta[property="og:description"]');
  const ogTitle = document.querySelector('meta[property="og:title"]');
  const ogLocale = document.querySelector('meta[property="og:locale"]');
  const ogLocaleAlternate = document.querySelector('meta[property="og:locale:alternate"]');
  const twitterTitle = document.querySelector('meta[name="twitter:title"]');
  const twitterDescription = document.querySelector('meta[name="twitter:description"]');

  // THESE STRINGS ARE THE RENDERED <title> AND description, NOT A FALLBACK.
  // setLanguage() assigns them unconditionally on every load, and a crawler
  // that runs JavaScript sees what this object says rather than what
  // index.html says. So when the head changes, this changes with it -- an
  // earlier SEO pass rewrote the <title> and left these behind, which would
  // have put the old strings straight back into the rendered DOM.
  //
  // AND `description` IS NOT `cardDescription`. index.html carries two strings
  // on purpose: the meta description is written for a search result and ends
  // "Early stage — not yet run on hardware.", while og:description and
  // twitter:description are written for a social card and end "Early
  // implementation — no board has run it yet." Assigning one to all three put
  // the search-result string on the card for every renderer-based crawler
  // while Facebook, X, Slack and Discord -- which do not run scripts -- read
  // the other from the HTML. One URL, two card texts. Found in review, in the
  // fix for the first version of this same defect.
  const copy = {
    en: {
      title: 'Attadipa — open-source ESP32-S3 smartwatch firmware, LoRa mesh, offline GNSS',
      ogTitle: 'Attadipa — open-source ESP32-S3 smartwatch firmware',
      locale: 'en_US',
      localeAlternate: 'ru_RU',
      description: 'Open-source ESP32-S3 smartwatch firmware: LoRa MeshCore messaging, offline GNSS navigation, LVGL UI on FreeRTOS. Early stage — not yet run on hardware.',
      cardDescription: 'LoRa MeshCore messaging, offline GNSS navigation and an LVGL UI on FreeRTOS, for two ESP32-S3 wearables. Early implementation — no board has run it yet.'
    },
    ru: {
      title: 'Attadipa — открытая прошивка для умных часов на ESP32-S3, LoRa mesh, GNSS офлайн',
      ogTitle: 'Attadipa — открытая прошивка для умных часов на ESP32-S3',
      locale: 'ru_RU',
      localeAlternate: 'en_US',
      description: 'Открытая прошивка для умных часов на ESP32-S3: LoRa-переписка через MeshCore, офлайн-навигация по GNSS, интерфейс LVGL на FreeRTOS. Ранняя стадия — на плате ещё не запускалась.',
      cardDescription: 'LoRa-переписка через MeshCore, офлайн-навигация по GNSS и интерфейс LVGL на FreeRTOS для двух носимых устройств на ESP32-S3. Ранняя стадия — ни на одной плате ещё не запускалась.'
    }
  };

  function browserLanguage() {
    const langs = navigator.languages?.length ? navigator.languages : [navigator.language || 'en'];
    // Prefer the browser's explicit language. Locale region is used only when it is actually RU.
    for (const value of langs) {
      const v = String(value || '').toLowerCase();
      if (v === 'ru' || v.startsWith('ru-')) return 'ru';
      try {
        const region = new Intl.Locale(value).region;
        if (region === 'RU') return 'ru';
      } catch (_) {}
    }
    return 'en';
  }

  function initialLanguage() {
    const url = new URL(location.href);
    const fromUrl = url.searchParams.get('lang');
    if (fromUrl === 'ru' || fromUrl === 'en') return fromUrl;
    // localStorage ACCESS THROWS, it does not return null: Chrome under
    // "block all cookies" and a sandboxed iframe both raise SecurityError on
    // the getter itself. Unguarded, the throw landed here -- inside
    // initialLanguage(), called before the IntersectionObserver block below --
    // so the whole IIFE aborted and nothing ever added `.visible`. The inline
    // script in index.html already wrapped the identical call; the guard was
    // known and not carried across. Found in review.
    let saved = null;
    try { saved = localStorage.getItem('attadipa-site-lang'); } catch (_) {}
    if (saved === 'ru' || saved === 'en') return saved;
    return browserLanguage();
  }

  function setLanguage(lang, {persist = true, updateUrl = true} = {}) {
    if (lang !== 'ru' && lang !== 'en') lang = 'en';
    html.lang = lang;
    document.title = copy[lang].title;
    if (metaDescription) metaDescription.content = copy[lang].description;
    if (ogDescription) ogDescription.content = copy[lang].cardDescription;
    if (ogTitle) ogTitle.content = copy[lang].ogTitle;
    if (ogLocale) ogLocale.content = copy[lang].locale;
    // og:locale:alternate names the OTHER language. Leaving it fixed made both
    // it and og:locale read ru_RU in Russian, which says the page has no other
    // language while sitting on the one that does.
    if (ogLocaleAlternate) ogLocaleAlternate.content = copy[lang].localeAlternate;
    if (twitterTitle) twitterTitle.content = copy[lang].ogTitle;
    if (twitterDescription) twitterDescription.content = copy[lang].cardDescription;
    buttons.forEach(btn => btn.setAttribute('aria-pressed', btn.dataset.setLang === lang ? 'true' : 'false'));
    // Same reason as the read above: the setter throws under the same
    // conditions, and losing the preference is not worth losing the page.
    if (persist) { try { localStorage.setItem('attadipa-site-lang', lang); } catch (_) {} }
    if (updateUrl) {
      const url = new URL(location.href);
      url.searchParams.set('lang', lang);
      history.replaceState(null, '', url);
    }
  }

  setLanguage(initialLanguage(), {persist: false, updateUrl: false});
  buttons.forEach(btn => btn.addEventListener('click', () => setLanguage(btn.dataset.setLang)));

  // HIDING IS OPT-IN, and the script is what opts in. `.reveal` ships visible;
  // `.js-reveal` on <html> is what makes it opacity:0, and it is added HERE --
  // one statement before the observer that undoes it.
  //
  // The previous arrangement had `.reveal{opacity:0}` in the stylesheet with a
  // <noscript> override, which covers exactly one of the ways this fails:
  // scripting DISABLED. It cannot cover scripting enabled and this file not
  // running -- a 404 on site.js, cached HTML opened offline, a blocking
  // extension, or anything above throwing -- because <noscript> does not render
  // when scripting is on. Those were the cases that produced a hero and empty
  // space, and they were the ones documented as fixed. Found in review.
  //
  // Inverted, every one of them lands on the readable page: no class, no
  // hiding. The animation is unchanged for everybody whose script runs.
  if ('IntersectionObserver' in window && !matchMedia('(prefers-reduced-motion: reduce)').matches) {
    html.classList.add('js-reveal');
    const observer = new IntersectionObserver(entries => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          entry.target.classList.add('visible');
          observer.unobserve(entry.target);
        }
      });
    }, {threshold: 0.08, rootMargin: '0px 0px -30px 0px'});
    document.querySelectorAll('.reveal').forEach(el => observer.observe(el));
  } else {
    document.querySelectorAll('.reveal').forEach(el => el.classList.add('visible'));
  }
})();
