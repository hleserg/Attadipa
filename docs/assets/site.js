(() => {
  const html = document.documentElement;
  const buttons = [...document.querySelectorAll('[data-set-lang]')];
  const metaDescription = document.querySelector('meta[name="description"]');
  const ogDescription = document.querySelector('meta[property="og:description"]');

  const copy = {
    en: {
      title: 'Firefly OS — Glow · Guide · Connect',
      description: 'Firefly OS is an open-source wearable firmware/application platform for ESP32-S3: mesh messaging, offline navigation, clean app APIs and a product-grade UI.'
    },
    ru: {
      title: 'Firefly OS — Glow · Guide · Connect',
      description: 'Firefly OS — открытая платформа прошивки и приложений для носимых устройств на ESP32-S3: mesh-связь, офлайн-навигация, чистые API и продуманный интерфейс.'
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
    const saved = localStorage.getItem('firefly-site-lang');
    if (saved === 'ru' || saved === 'en') return saved;
    return browserLanguage();
  }

  function setLanguage(lang, {persist = true, updateUrl = true} = {}) {
    if (lang !== 'ru' && lang !== 'en') lang = 'en';
    html.lang = lang;
    document.title = copy[lang].title;
    if (metaDescription) metaDescription.content = copy[lang].description;
    if (ogDescription) ogDescription.content = copy[lang].description;
    buttons.forEach(btn => btn.setAttribute('aria-pressed', btn.dataset.setLang === lang ? 'true' : 'false'));
    if (persist) localStorage.setItem('firefly-site-lang', lang);
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
