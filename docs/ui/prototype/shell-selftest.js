// Run in shell.html's browser context. See SHELL_STUDY_03.md for the command.
// Catches broken destinations/returns, fictional battery percentages, hidden
// unavailable apps, clipped controls and lost review data. No firmware claims.
(async () => {
  const failures = [];
  let checks = 0;
  const check = (condition, message) => {
    checks++;
    if (!condition) failures.push(message);
  };
  check(!!document.querySelector('#watch-shell'), 'The interactive shell is present');
  if (failures.length) return { checks, failures };
  const storageBefore = JSON.stringify(Object.entries(localStorage).sort());
  const select = (id, value) => {
    const control = document.getElementById(id);
    control.value = value;
    control.dispatchEvent(new Event('change', { bubbles: true }));
  };
  const click = (route) => {
    const button = document.querySelector(`#watch-shell [data-go="${route}"]`);
    check(!!button, `Route ${route} has a visible affordance`);
    if (button) { button.focus(); button.click(); }
  };
  const escape = () => {
    const event = new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true });
    document.activeElement.dispatchEvent(event);
    return event.defaultPrevented;
  };
  const luminance = color => color.match(/[\d.]+/g).slice(0, 3)
    .map(v => Number(v) / 255).map(v => v <= .04045 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4)
    .reduce((sum, v, i) => sum + v * [.2126, .7152, .0722][i], 0);
  const connectedContrast = context => {
    const mark = document.querySelector('.link-mark');
    const foreground = luminance(getComputedStyle(mark).color);
    const background = luminance(getComputedStyle(document.querySelector('.status')).backgroundColor);
    check((Math.max(foreground, background) + .05) / (Math.min(foreground, background) + .05) >= 3,
      `${context}: connected glyph clears graphic contrast`);
  };
  const page = (name) => check(
    document.querySelector('#watch-shell').dataset.page === name,
    `Expected page ${name}`,
  );
  const fit = (context) => {
    const screen = document.querySelector('#watch-shell');
    const bounds = screen.getBoundingClientRect();
    const target = parseFloat(getComputedStyle(screen).getPropertyValue('--target'));
    const content = screen.querySelector('.content');
    check(content.scrollHeight <= content.clientHeight + 1, `${context}: no page overflow`);
    const status = screen.querySelector('.status');
    check(status.scrollWidth <= status.clientWidth + 1, `${context}: status fits`);
    for (const scroll of screen.querySelectorAll('.scroll')) scroll.scrollTop = 0;
    for (const button of screen.querySelectorAll('button')) {
      button.scrollIntoView({ block: 'nearest', inline: 'nearest' });
      const r = button.getBoundingClientRect();
      check(r.width >= target - 1 && r.height >= target - 1, `${context}: touch target ${button.textContent}`);
      check(r.left >= bounds.left && r.right <= bounds.right + 1 &&
        r.top >= bounds.top && r.bottom <= bounds.bottom + 1,
      `${context}: reachable control ${button.textContent}`);
      check(button.scrollWidth <= button.clientWidth + 1 &&
        button.scrollHeight <= button.clientHeight + 1, `${context}: label fits ${button.textContent}`);
    }
    for (const scroll of screen.querySelectorAll('.scroll')) scroll.scrollTop = 0;
  };
  await document.fonts.ready;
  check([...document.fonts].some(f => f.family.includes('Nunito') && f.status === 'loaded'), 'Bundled font loads');
  for (const src of ['glade-day-v2.png', 'glade-night-v2.png']) {
    const raster = new Image();
    raster.src = src;
    try { await raster.decode(); } catch { /* Assert the actual load below. */ }
    check(raster.naturalWidth > 0, `Bundled artwork loads: ${src}`);
  }
  for (const size of ['small', 'large']) {
    select('size', size);
    for (const locale of ['ru', 'en']) {
      select('locale', locale);
      for (const link of document.querySelectorAll('.wordmark, .previous')) {
        const destination = new URL(link.href);
        check(destination.pathname.endsWith('/index.html') && destination.searchParams.get('v') === '2' &&
          destination.searchParams.get('locale') === locale, 'Both previous-demo links retain the selected locale');
      }
      for (const theme of ['night', 'day']) {
        select('theme', theme);
        const context = `${size}/${locale}/${theme}`;
        select('motion', 'ambient');
        select('scenario', 'fresh');
        document.querySelector('#restart').click();
        page('clock'); fit(`${context}/clock`);
        connectedContrast(context);
        document.querySelector('#locale').focus();
        check(!escape() && document.activeElement.id === 'locale', 'Escape leaves review controls focused and unconsumed');
        page('clock');
        document.querySelector('[data-go="apps"]').focus();
        check(!escape(), 'Escape at home has no back action');
        click('apps'); page('apps'); fit(`${context}/apps`);
        check(!!document.querySelector('[data-go="navigation"]'), 'Navigation is installed even when unavailable');
        check(!document.querySelector('[data-go="mesh"]'), 'Mesh is not falsely listed as a registered app');
        click('navigation'); page('navigation'); fit(`${context}/navigation`);
        check(!document.querySelector('.needle'), 'No invented heading or direction needle');
        click('back'); page('apps');
        click('settings'); page('settings'); fit(`${context}/settings`);
        const settingsList = document.querySelector('#watch-shell .rows');
        const settingsHeading = document.querySelector('#watch-shell h2');
        const cue = settingsHeading.querySelector('[aria-hidden="true"]');
        check(!!cue === (settingsList.scrollHeight > settingsList.clientHeight + 1),
          `${context}: scroll cue matches actual overflow`);
        const spokenHeading = settingsHeading.cloneNode(true);
        spokenHeading.querySelectorAll('[aria-hidden="true"]').forEach(el => el.remove());
        check(spokenHeading.textContent.trim() === (locale === 'ru' ? 'Настройки' : 'Settings'),
          'The Settings accessible heading does not include a decorative arrow');
        document.querySelector('#theme').focus();
        check(!escape() && document.activeElement.id === 'theme', 'Escape outside the shell does not navigate history');
        page('settings');
        click('connection'); page('connection'); fit(`${context}/connection`);
        check(document.querySelector('.node-reading').textContent.includes(locale === 'ru' ? '4,02' : '4.02'), 'Fresh node reading is voltage');
        check(!document.querySelector('.node-status').textContent.includes('%'), 'Node voltage is not converted to percent');
        const announcedNode = document.querySelector('.node-status');
        check(announcedNode.getAttribute('role') === 'img' &&
          announcedNode.getAttribute('aria-label').includes(locale === 'ru' ? '4,02' : '4.02'),
        'Assistive technology receives the node reading and its state');
        const freshAnnouncement = announcedNode.getAttribute('aria-label');
        for (const state of ['stale', 'unknown', 'disconnected', 'low', 'integrated']) {
          select('scenario', state);
          page('connection'); fit(`${context}/${state}`);
          const node = document.querySelector('.node-status');
          if (state === 'integrated') {
            check(!node, 'Integrated source has no duplicate battery');
          } else {
            check(!!node, `${state}: accessory status remains visible`);
            if (state === 'low') {
              connectedContrast(`${context}/low`);
              check(node.getAttribute('aria-label') === freshAnnouncement,
                'A low watch battery does not change the connected node announcement');
              check(document.querySelector('.watch-status').textContent.includes('8%'), 'Low battery belongs to the watch');
            }
            check(!node.textContent.includes('%'), `${state}: no fabricated SOC`);
            if (['stale', 'unknown', 'disconnected'].includes(state))
              check(node.textContent.includes('—'), `${state}: no fresh-looking battery value`);
          }
        }
        select('scenario', 'disconnected');
        click('back'); page('settings');
        click('back'); page('apps');
        check(!!document.querySelector('[data-go="navigation"]'), 'Disconnect does not hide installed navigation');
        click('settings'); click('display'); page('display'); fit(`${context}/display`);
        const oldTheme = document.querySelector('#theme').value;
        click('toggle-theme');
        check(document.querySelector('#theme').value !== oldTheme, 'Watch theme control updates the actual display');
        check(document.activeElement.dataset.go === 'toggle-theme', 'Theme change preserves control focus');
        click('toggle-theme');
        click('toggle-motion');
        check(document.querySelector('#motion').value === 'reduce', 'Watch control reduces motion');
        check(getComputedStyle(document.querySelector('.fireflies i')).animationName === 'none', 'Reduced motion actually stops the decorative animation');
        click('back'); page('settings');
        click('about'); page('about'); fit(`${context}/about`);
        const cancelled = new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true });
        cancelled.preventDefault();
        document.activeElement.dispatchEvent(cancelled);
        page('about');
        check(escape(), 'Escape from the watch consumes a real back action');
        page('settings');
        const returned = document.activeElement;
        const returnedBounds = returned.getBoundingClientRect();
        const listBounds = document.querySelector('#watch-shell .scroll').getBoundingClientRect();
        check(returned.dataset.go === 'about' && returnedBounds.top >= listBounds.top &&
          returnedBounds.bottom <= listBounds.bottom + 1,
        'Back restores visible keyboard focus to the scrolled Settings row');
        click('back'); page('apps'); click('clock'); page('clock');
        select('motion', 'ambient');
      }
    }
  }
  // This slice must not quietly gain an unimplemented composer or touch V2 storage.
  check(!document.querySelector('#watch-shell input, #watch-shell textarea'), 'No unimplemented free-text composer');
  if (matchMedia('(prefers-reduced-motion: reduce)').matches)
    check(getComputedStyle(document.querySelector('.fireflies i')).animationName === 'none', 'System reduced motion overrides the ambient option');
  check(storageBefore === JSON.stringify(Object.entries(localStorage).sort()), 'Old review records remain untouched');
  document.querySelector('#restart').click();
  select('size', 'small'); select('locale', 'ru'); select('theme', 'night'); select('scenario', 'fresh');
  document.querySelector('#test-result').textContent = `${checks} checks; ${failures.length} failures`;
  return { checks, failures, scope: 'Browser prototype only; NOT EXECUTED — HARDWARE REQUIRED' };
})();
