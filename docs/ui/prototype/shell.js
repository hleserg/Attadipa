(() => {
  'use strict';
  // Deliberately no storage, BLE, timers for telemetry, or firmware validity logic.
  // This presentation fixture mirrors #496's clock/navigation/settings inventory.
  const copy = {
    ru: {
      eyebrow: 'Один мир на запястье', headline: 'Всё начинается<br>с одного касания.',
      intro: 'Циферблат, приложения и настройки — теперь связаны. Нажимайте на экран часов.',
      simulation: 'ДЕМО · ДАННЫЕ ВЫМЫШЛЕНЫ', restart: 'К циферблату',
      sizeNote: 'Реальные пиксельные размеры; не физический масштаб. На узком окне большой экран можно прокрутить по горизонтали.',
      reviewTitle: 'Посмотреть в разных условиях', size: 'Экран', locale: 'Язык', theme: 'Тема', night: 'Ночь', day: 'День',
      motion: 'Движение', ambient: 'Светлячки', reduce: 'Без анимации', scenario: 'Состояние ноды · демонстрация',
      fresh: 'Подключена · свежие данные', stale: 'Подключена · данные устарели', unknown: 'Подключена · батарея неизвестна',
      disconnected: 'Связь потеряна', low: 'Часы разряжены · нода подключена', integrated: 'Встроенный источник · одна батарея',
      now: 'На экране', boundary: 'Это веб-прототип, не прошивка. Он не подключается к BLE и ничего не отправляет. Батареи и время — примеры; настройки действуют только до перезагрузки страницы.',
      previous: 'Ранее согласованные экраны ↗', footer: 'Красота — в спокойствии. Ясность — в каждом состоянии.',
      clock: 'Циферблат', apps: 'Приложения', settings: 'Настройки', connection: 'Связь и нода', display: 'Экран', about: 'Об устройстве', navigation: 'Навигация',
      back: 'Назад', watch: 'Часы', node: 'Нода', motto: 'Приключение рядом', needPosition: 'Нужна позиция', needNode: 'Подключите ноду',
      noTarget: 'Цель не выбрана', noPosition: 'Нет подтверждённой позиции.', noDirection: 'Без позиции и цели расстояние и направление не показаны.',
      connected: 'Нода подключена', lost: 'Нет связи с нодой', staleReading: 'Батарея: данные устарели', unknownReading: 'Батарея: нет данных',
      freshAge: 'Получено 3 с назад', staleAge: 'Последнее: 4,02 В · 5 мин назад', volts: '4,02 В',
      voltageNote: 'Нода сообщает напряжение, не процент заряда.', integratedTitle: 'Источник встроен', integratedNote: 'Один источник питания. Батарея уже показана как «Часы».',
      demoOnly: 'Демонстрационные данные', themeDay: 'Тема: день', themeNight: 'Тема: ночь',
      motionOff: 'Движение: выкл.', motionOn: 'Светлячки: вкл.', systemMotion: 'Система: без движения',
      previewSettings: 'Настройки веб-демо', aboutText: 'Shell Study 03<br>Интерактивный макет', aboutNote: 'Меш, контакты и ввод сообщений — следующий этап. Эта страница не управляет устройством.',
      clockNote: 'Время — главное. Один вход в приложения. Статус часов и носимой ноды остаётся на каждом активном экране.',
      appsNote: 'Циферблат — домашний экран; Навигация и Настройки — остальные приложения реестра #496. Отключение ноды не прячет установленное приложение.',
      settingsNote: 'Настройки доступны всегда. Здесь можно посмотреть состояние ноды и изменить оформление самого демо; это не перепрошивка и не настройка BLE.',
      connectionNote: 'Выберите состояние справа: свежие данные, устаревшие, неизвестные или потеря связи. При встроенном источнике второй батареи нет. Все значения — примеры.',
      displayNote: 'Тема и движение меняются прямо на часах. Выбор не сохраняется после перезагрузки. Системное уменьшение движения всегда учитывается.',
      aboutPageNote: 'Это отдельное исследование оболочки. Старое демо и его оценки не изменены. Работа на железе и читаемость на солнце здесь не проверялись.',
      navigationNote: 'Пока нет выбранной удалённой цели и подтверждённой собственной позиции. Красивой стрелки вместо отсутствующих данных не будет. Выбор контакта требует следующего инженерного этапа.',
    },
    en: {
      eyebrow: 'One world on your wrist', headline: 'It starts with<br>a single touch.',
      intro: 'The watch face, apps and settings are connected. Tap the watch to explore.', simulation: 'DEMO · SIMULATED DATA', restart: 'Watch face',
      sizeNote: 'Native pixel dimensions, not physical scale. Scroll horizontally to inspect the large display in a narrow window.',
      reviewTitle: 'Explore different conditions', size: 'Display', locale: 'Language', theme: 'Theme', night: 'Night', day: 'Day',
      motion: 'Motion', ambient: 'Fireflies', reduce: 'Reduce motion', scenario: 'Node state · simulation',
      fresh: 'Connected · recent data', stale: 'Connected · stale data', unknown: 'Connected · battery unknown',
      disconnected: 'Connection lost', low: 'Watch battery low · node connected', integrated: 'Integrated source · one battery',
      now: 'On the display', boundary: 'A browser prototype, not firmware. No BLE connection or message sending. Battery and time values are examples; settings last only until page reload.',
      previous: 'Previously approved screens ↗', footer: 'Quiet beauty. Clarity in every state.',
      clock: 'Watch face', apps: 'Applications', settings: 'Settings', connection: 'Connection & node', display: 'Display', about: 'About', navigation: 'Navigation',
      back: 'Back', watch: 'Watch', node: 'Node', motto: 'A little light, always', needPosition: 'Position needed', needNode: 'Connect your node',
      noTarget: 'No target selected', noPosition: 'No verified position.', noDirection: 'Without position and target, distance and direction are not shown.',
      connected: 'Node connected', lost: 'Node disconnected', staleReading: 'Battery data is stale', unknownReading: 'Battery data unavailable',
      freshAge: 'Received 3 s ago', staleAge: 'Last: 4.02 V · 5 min ago', volts: '4.02 V',
      voltageNote: 'The node reports voltage, not charge percentage.', integratedTitle: 'Integrated source', integratedNote: 'One power source. Its battery is already shown as Watch.',
      demoOnly: 'Simulated data', themeDay: 'Theme: day', themeNight: 'Theme: night',
      motionOff: 'Motion: off', motionOn: 'Fireflies: on', systemMotion: 'System: motion off',
      previewSettings: 'Browser demo settings', aboutText: 'Shell Study 03<br>Interactive prototype', aboutNote: 'Mesh, contacts and composing messages come next. This page does not control a device.',
      clockNote: 'Time comes first. One entrance to your apps. Watch and carried-node status remain on every active display.',
      appsNote: 'Clock is home; Navigation and Settings are the other apps in registry #496. Disconnecting a node does not hide an installed app.',
      settingsNote: 'Settings are always available. Inspect node state or change the demo display; this does not configure firmware or BLE.',
      connectionNote: 'Use the selector to inspect recent, stale, unknown and disconnected states. An integrated source has no second battery. All readings are simulated.',
      displayNote: 'Theme and motion change from the watch itself. Choices do not survive reload. System reduced-motion preferences always take priority.',
      aboutPageNote: 'A separate shell study. The previous demo and its reviews are unchanged. Hardware operation and outdoor readability have not been tested here.',
      navigationNote: 'No remote target has been selected and no own position is verified. Missing data never becomes a decorative direction arrow. Contact selection needs the next engineering slice.',
    },
  };
  const ids = ['size', 'locale', 'theme', 'motion', 'scenario'];
  const params = new URLSearchParams(location.search);
  for (const id of ids) {
    const select = document.getElementById(id);
    const value = params.get(id);
    if ([...select.options].some(option => option.value === value)) select.value = value;
  }
  const shell = document.getElementById('watch-shell');
  const content = shell.querySelector('.content');
  const status = shell.querySelector('.status');
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const history = [];
  let page = 'clock';
  const value = id => document.getElementById(id).value;
  const t = key => copy[value('locale')][key];
  const shapes = {
    navigation: '<path d="m12 3 8 18-8-5-8 5 8-18Z"/>',
    settings: '<circle cx="12" cy="12" r="4"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3M5 5l2 2m10 10 2 2M5 19l2-2M17 7l2-2"/>',
    connection: '<rect x="8" y="3" width="8" height="18" rx="3"/><rect x="3" y="8" width="18" height="8" rx="3"/>',
    display: '<rect x="5" y="2" width="14" height="20" rx="4"/><path d="M10 18h4"/>',
    about: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-11v1"/>',
  };
  const icon = name => `<svg class="icon" viewBox="0 0 24 24" aria-hidden="true">${shapes[name]}</svg>`;
  const button = (go, label, kind = '') => `<button type="button" data-go="${go}" class="${kind}">${label}</button>`;
  const row = (go, label, subtitle = '') => button(go, `${icon(go)}<span class="row-label">${label}${subtitle ? `<small>${subtitle}</small>` : ''}</span><span class="chevron" aria-hidden="true">›</span>`);
  const heading = title => `<h2 tabindex="-1"><span>${title}</span></h2>`;
  const scroll = body => `<div class="scroll detail" tabindex="0">${body}</div>`;
  const back = () => button('back', t('back'));
  function renderStatus() {
    const scenario = value('scenario');
    const low = scenario === 'low';
    const charge = low ? '8%' : '82%';
    const watch = `<span class="watch-status">${low ? '! ' : ''}${t('watch')} <span class="battery ${low ? 'low' : ''}" aria-hidden="true"><i></i></span> ${charge}</span>`;
    const recent = scenario === 'fresh' || scenario === 'low';
    const mark = recent ? '✓' : scenario === 'disconnected' ? '×' : '!';
    const description = `${t('node')}: ${t(recent ? 'fresh' : scenario)}${recent ? ` · ${t('volts')} · ${t('freshAge')}` : ''}`;
    status.innerHTML = watch + (scenario === 'integrated' ? '' : `<span class="node-status" role="img" aria-label="${description}"><span class="${recent ? 'link-mark' : 'attention'}" aria-hidden="true">${mark}</span> ${t('node')} ${recent ? t('volts') : '—'}</span>`);
  }
  function connectionBody() {
    const scenario = value('scenario');
    if (scenario === 'integrated') return `<p>${t('integratedTitle')}</p><p class="caption">${t('integratedNote')}</p>`;
    const recent = scenario === 'fresh' || scenario === 'low';
    const state = scenario === 'disconnected' ? t('lost') : t('connected');
    const reading = recent ? t('volts') : '—';
    const explanation = recent ? t('freshAge') : scenario === 'stale' ? t('staleReading') : t('unknownReading');
    return `<p>${state}</p><p class="node-reading">${reading}</p><p class="caption">${explanation}</p>${['stale', 'disconnected'].includes(scenario) ? `<p class="caption">${t('staleAge')}</p>` : ''}<hr><p class="caption">${t('voltageNote')}</p>`;
  }
  function render(focus = '') {
    document.documentElement.lang = value('locale');
    for (const link of document.querySelectorAll('.wordmark, .previous')) link.href = `index.html?v=2&locale=${value('locale')}`;
    for (const element of document.querySelectorAll('[data-copy]')) element.innerHTML = t(element.dataset.copy);
    shell.dataset.size = value('size'); shell.dataset.theme = value('theme'); shell.dataset.motion = value('motion'); shell.dataset.page = page;
    document.getElementById('geometry').textContent = value('size') === 'small' ? '240 × 240' : '410 × 502';
    document.getElementById('page-title').textContent = t(page);
    document.getElementById('page-note').textContent = t(page === 'about' ? 'aboutPageNote' : `${page}Note`);
    document.getElementById('breadcrumb').textContent = [t('clock'), ...(page === 'clock' ? [] : [t(page)])].join(' / ');
    renderStatus();
    if (page === 'clock') {
      const date = new Intl.DateTimeFormat(value('locale') === 'ru' ? 'ru-RU' : 'en-GB', { weekday: 'short', day: 'numeric', month: 'long', timeZone: 'UTC' }).format(new Date('2026-09-09T10:09:00Z'));
      content.innerHTML = `<div class="clock-hero"><p class="date reading">${date}</p><p class="time">10:09</p><p class="motto reading">${t('motto')}</p></div>${button('apps', t('apps'), 'primary')}`;
    } else if (page === 'apps') {
      content.innerHTML = `${heading(t('apps'))}<div class="scroll rows">${row('navigation', t('navigation'), value('scenario') === 'disconnected' ? t('needNode') : t('needPosition'))}${row('settings', t('settings'))}</div>${button('clock', t('restart'))}`;
    } else if (page === 'settings') {
      content.innerHTML = `${heading(t('settings'))}<div class="scroll rows">${row('connection', t('connection'))}${row('display', t('display'))}${row('about', t('about'))}</div>${back()}`;
      const rows = content.querySelector('.rows');
      if (rows.scrollHeight > rows.clientHeight + 1) content.querySelector('h2 > span').insertAdjacentHTML('beforeend', '<span aria-hidden="true"> ↕</span>');
    } else if (page === 'connection') {
      content.innerHTML = `${heading(t('node'))}${scroll(connectionBody())}${back()}`;
    } else if (page === 'display') {
      const motion = reduced.matches ? t('systemMotion') : value('motion') === 'reduce' ? t('motionOff') : t('motionOn');
      content.innerHTML = `${heading(t('display'))}<div class="scroll rows">${button('toggle-theme', value('theme') === 'night' ? t('themeNight') : t('themeDay'))}${button('toggle-motion', motion)}</div>${back()}`;
    } else if (page === 'navigation') {
      content.innerHTML = `${heading(t('navigation'))}${scroll(`<p>${t('noTarget')}</p><p class="caption">${t('noPosition')}</p><p class="caption">${t('noDirection')}</p>`)}${back()}`;
    } else {
      content.innerHTML = `${heading(t('about'))}${scroll(`<p>${t('aboutText')}</p><p class="caption">${t('aboutNote')}</p>`)}${back()}`;
    }
    if (focus) {
      const target = content.querySelector(`[data-go="${focus}"]`) || content.querySelector('h2, button');
      target?.focus({ preventScroll: true });
      target?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    }
  }
  function go(route) {
    if (route === 'toggle-theme' || route === 'toggle-motion') {
      const control = document.getElementById(route === 'toggle-theme' ? 'theme' : 'motion');
      control.value = route === 'toggle-theme' ? (control.value === 'night' ? 'day' : 'night') : (control.value === 'ambient' ? 'reduce' : 'ambient');
      render(route); return;
    }
    if (route === 'clock') { history.length = 0; page = 'clock'; render('apps'); return; }
    if (route === 'back') {
      const previous = history.pop();
      page = previous?.page || 'clock'; render(previous?.focus || 'apps'); return;
    }
    if (!['apps', 'settings', 'connection', 'display', 'about', 'navigation'].includes(route)) return;
    history.push({ page, focus: route }); page = route; render('heading');
  }
  content.addEventListener('click', event => {
    const button = event.target.closest('button[data-go]');
    if (button) go(button.dataset.go);
  });
  content.addEventListener('keydown', event => {
    if (event.key === 'Escape' && history.length && !event.defaultPrevented) { event.preventDefault(); go('back'); }
  });
  document.getElementById('restart').addEventListener('click', () => go('clock'));
  for (const id of ids) document.getElementById(id).addEventListener('change', () => render());
  reduced.addEventListener('change', () => render());
  render();
})();
