(() => {
  const html = document.documentElement;
  const buttons = [...document.querySelectorAll('[data-set-lang]')];
  const metaDescription = document.querySelector('meta[name="description"]');
  const ogDescription = document.querySelector('meta[property="og:description"]');
  const ogTitle = document.querySelector('meta[property="og:title"]');
  const ogLocale = document.querySelector('meta[property="og:locale"]');
  const twitterTitle = document.querySelector('meta[name="twitter:title"]');
  const twitterDescription = document.querySelector('meta[name="twitter:description"]');

  // THESE STRINGS ARE THE RENDERED <title> AND description, NOT A FALLBACK.
  // setLanguage() assigns them unconditionally on every load, and a crawler
  // that runs JavaScript sees what this object says rather than what
  // index.html says. So when the head changes, this changes with it -- an
  // earlier SEO pass rewrote the <title> and left these behind, which would
  // have put the old strings straight back into the rendered DOM.
  const copy = {
    en: {
      title: 'Attadipa — open-source ESP32-S3 smartwatch firmware, LoRa mesh, offline GNSS',
      ogTitle: 'Attadipa — open-source ESP32-S3 smartwatch firmware',
      locale: 'en_US',
      description: 'Open-source ESP32-S3 smartwatch firmware: LoRa MeshCore messaging, offline GNSS navigation, LVGL UI on FreeRTOS. Early stage — not yet run on hardware.'
    },
    ru: {
      title: 'Attadipa — открытая прошивка для умных часов на ESP32-S3, LoRa mesh, GNSS офлайн',
      ogTitle: 'Attadipa — открытая прошивка для умных часов на ESP32-S3',
      locale: 'ru_RU',
      description: 'Открытая прошивка для умных часов на ESP32-S3: LoRa-переписка через MeshCore, офлайн-навигация по GNSS, интерфейс LVGL на FreeRTOS. Ранняя стадия — на плате ещё не запускалась.'
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
    const saved = localStorage.getItem('attadipa-site-lang');
    if (saved === 'ru' || saved === 'en') return saved;
    return browserLanguage();
  }

  function setLanguage(lang, {persist = true, updateUrl = true} = {}) {
    if (lang !== 'ru' && lang !== 'en') lang = 'en';
    html.lang = lang;
    document.title = copy[lang].title;
    if (metaDescription) metaDescription.content = copy[lang].description;
    if (ogDescription) ogDescription.content = copy[lang].description;
    if (ogTitle) ogTitle.content = copy[lang].ogTitle;
    if (ogLocale) ogLocale.content = copy[lang].locale;
    if (twitterTitle) twitterTitle.content = copy[lang].ogTitle;
    if (twitterDescription) twitterDescription.content = copy[lang].description;
    buttons.forEach(btn => btn.setAttribute('aria-pressed', btn.dataset.setLang === lang ? 'true' : 'false'));
    if (persist) localStorage.setItem('attadipa-site-lang', lang);
    if (updateUrl) {
      const url = new URL(location.href);
      url.searchParams.set('lang', lang);
      history.replaceState(null, '', url);
    }
  }

  setLanguage(initialLanguage(), {persist: false, updateUrl: false});
  buttons.forEach(btn => btn.addEventListener('click', () => setLanguage(btn.dataset.setLang)));

  if ('IntersectionObserver' in window && !matchMedia('(prefers-reduced-motion: reduce)').matches) {
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
