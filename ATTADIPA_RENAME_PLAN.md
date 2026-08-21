# FireflyOS → Attadipa: полный план переименования

**Дата аудита:** 2026-08-21  
**Репозиторий на момент аудита:** `hleserg/FireflyOS`, ветка `main`  
**Новое каноническое имя проекта:** **Attadipa**  
**Маскот:** **Lumar** — светлячок  
**Архитектурное кредо:** **Independent by design.** Само имя Attadipa отсылает к палийскому *attadīpa* — «опирающийся на себя / имеющий себя островом и прибежищем». Ключевые возможности устройства не должны зависеть от телефона, облака или внешней инфраструктуры, если технически могут выполняться локально.

---

## 0. Канонические правила именования

До начала массовых правок зафиксировать одну таблицу, чтобы не получить смесь `Attadipa`, `AttadipaOS`, `attadipa-os`, `Firefly` и старых технических идентификаторов.

| Что | Было | Должно стать |
|---|---|---|
| Бренд проекта | Firefly OS / FireflyOS | **Attadipa** |
| Репозиторий | `hleserg/FireflyOS` | **`hleserg/Attadipa`** |
| Полное описание | Firefly OS | **Attadipa — open wearable platform** |
| Маскот | безымянный firefly | **Lumar** |
| Внешняя нода | Firefly Node | **Attadipa Node** |
| Приложения | Firefly Apps | **Attadipa Apps** |
| Companion | Firefly Companion | **Attadipa Companion** |
| C/C++ namespace | `firefly` | **`attadipa`** |
| CMake project | `firefly` | **`attadipa`** |
| CMake targets | `firefly_*` | **`attadipa_*`** |
| CMake aliases | `firefly::*` | **`attadipa::*`** |
| Macro prefix | `FIREFLY_*` | **`ATTADIPA_*`** |
| Public include path | `firefly/...` | **`attadipa/...`** |
| Public include dirs | `*/include/firefly/` | **`*/include/attadipa/`** |
| Файлы/модули с именем | `Firefly*.cmake` и т. п. | **`Attadipa*.cmake`** |
| Simulator binary | `firefly_sim` | **`attadipa_sim`** |
| Web localStorage keys | `firefly-*` | **`attadipa-*`** |
| Pages path | `/FireflyOS/` | **`/Attadipa/`** до появления custom domain |

**Не использовать `AttadipaOS` как новое основное имя.** Проект шире классической ОС, а выбранный бренд — `Attadipa`. `OS` можно употреблять только там, где это действительно технически необходимо или исторически объясняется.

**Слово `firefly` как обычное английское слово не запрещается.** Оно может оставаться в описании Lumar как вида насекомого. Удалять нужно старое **брендовое** употребление Firefly/FireflyOS и технические идентификаторы.

---

# 1. До публичного переименования

Это нужно сделать **до** смены имени репозитория и публикации анонса.

## 1.1. Финальный name clearance и резервирование — ПРОВЕРЕНО 2026-08-21

### Решение: **GREEN/YELLOW — имя можно фиксировать**

У **Attadipa** в ходе расширенного preliminary clearance не обнаружено прямого exact-name конфликта в embedded software, smartwatch firmware, ESP32, navigation, LoRa/mesh или wearable software.

При этом имя не абсолютно пустое: есть несколько существующих употреблений, которые нужно осознанно принять.

#### Товарные знаки

- [x] Выполнен targeted exact-name поиск `ATTADIPA` по индексируемым результатам USPTO, EUIPO, WIPO и российских баз, включая запросы по software/electronics/wearables и классам 9/42.
- [x] **Очевидного exact word mark `ATTADIPA` в software, embedded firmware, smartwatches, wearable electronics, communications или navigation не найдено.**
- [x] Отдельно проверены запросы `ATTADIPA trademark class 9`, `class 42`, `smartwatch`, `wearable`, `electronics`.
- [ ] Если проект станет коммерческим hardware-брендом с заметными продажами, перед регистрацией собственного знака заказать профессиональный clearance у патентного поверенного. Текущий результат — предварительный, не юридическое заключение.

#### Реальные компании и бренды

- [x] Найдено **PT Attadipa Elektro** (Сурабая, Индонезия). Компания реально существует и работает в промышленной автоматизации, instrumentation, control systems, electrical engineering и telecom/communication systems.
- [x] Это **не exact standalone software-brand `Attadipa`**, а полное фирменное имя `Attadipa Elektro`; smartwatch/firmware-продукта под именем Attadipa у неё не найдено.
- [x] Найден японский wellness/healing-проект **attaDIPA** на Wix. Область полностью посторонняя.
- [x] Основной поисковый массив по слову Attadipa — буддийские/палийские тексты и исследования, а не коммерческие software/hardware-продукты.

**Оценка риска от PT Attadipa Elektro:** низкий/умеренный. Это ближайшее найденное техническое совпадение, но оно географически, продуктово и по полному имени достаточно далеко от open-source smartwatch platform. Оно не является основанием отказываться от названия, но должно быть записано в brand-clearance history.

#### GitHub

- [x] Global repository search по exact `Attadipa` не показал проекта, который конкурирует с нашим по firmware/ESP32/wearables.
- [x] Запросы `Attadipa ESP32`, `Attadipa firmware`, `Attadipa smartwatch`, `Attadipa LoRa` не выявили прямого конкурента.
- [x] Репозиторий **`hleserg/Attadipa`** на момент проверки не существует и должен быть доступен для rename текущего repo.
- [x] **GitHub user `Attadipa` уже занят** реальным пользователем (у него есть, например, `Attadipa/TIL` и `Attadipa/semiGitTestRepo`).
- [x] Следствие: exact organization/user handle `github.com/Attadipa` нам недоступен, если владелец его не освободит. Это **не мешает** использовать `github.com/hleserg/Attadipa`.

#### Package namespaces

Targeted search не выявил exact package/component `attadipa` в:

- [x] PyPI — индексируемого exact package не найдено;
- [x] npm — индексируемого exact package не найдено;
- [x] crates.io — индексируемого exact crate не найдено;
- [x] PlatformIO Registry — exact package не найден;
- [x] ESP Component Registry — exact component не найден.

Перед первой публикацией пакета всё равно проверить registry напрямую и занять нужные namespaces. Отсутствие результатов поисковика не является гарантией live availability.

#### Домены

- [x] `attadipa.com` встречается в списке **expired/deleted domains за январь 2015 года**.
- [x] В текущем web-index не найден работающий публичный сайт на `attadipa.com`, `attadipa.org`, `attadipa.net`, `attadipa.io`, `attadipa.dev`, `attadipa.app`, `attadipa.tech`, `attadipa.watch` или `attadipa.systems`.
- [ ] **Live registrar/RDAP availability из текущей среды достоверно подтвердить не удалось.** Отсутствие сайта/индексации не означает, что домен свободен.
- [ ] До публичного анонса открыть registrar и проверить/зарегистрировать лучший доступный домен.
- [ ] Если exact `.com` доступен — брать его первым.
- [ ] Если `.com` занят/дорог, приоритет: `attadipa.dev`, `attadipa.org`, `attadipa.io` либо хороший compound domain без изменения бренда.

#### Social / community handles

- [x] Exact X/Twitter handle **`@attadipa` занят** японским пользователем как минимум с 2009 года.
- [ ] Telegram / Discord / Matrix / YouTube / Instagram проверить live перед созданием официальных каналов.
- [ ] Не менять бренд только ради exact social handle; допустимы `attadipa_dev`, `attadipa_os`, `getattadipa` и подобные служебные handles.

### Search/SEO особенность

`Attadipa` — настоящий палийский/буддийский термин и название сутты/раздела канона. Поэтому generic search `Attadipa` ещё долго будет смешивать проект с буддийскими материалами.

Это **осознанный trade-off**, а не collision:

- плюс: у имени настоящий смысл и сильная история;
- плюс: почти нет software/embedded brand clutter;
- минус: мы не будем владеть generic SERP по слову с первого дня;
- решение: в title/description стабильно использовать `Attadipa wearable platform`, `Attadipa ESP32`, `Attadipa firmware`.

### Смысл имени

Каноническая форма — палийское **attadīpa**. Словари передают её как *relying on oneself, independent, founded on oneself*. В известной формуле `attadīpā viharatha, attasaraṇā anaññasaraṇā` смысл — иметь себя «островом/опорой/прибежищем», не искать внешнего прибежища.

Для проекта это напрямую соответствует архитектуре: телефон, облако и Интернет могут расширять возможности устройства, но не должны становиться его обязательным мозгом.

**Каноническое русское написание бренда:** **Аттадипа**.  
**Рекомендуемое произношение:** **атта-ДИ-па**.  
**Каноническое латинское написание:** **Attadipa** (без диакритики в бренде и коде).  
**Палийскую форму `Attadīpa`** использовать только при объяснении происхождения имени.

### Итог 1.1

**Имя фиксируем: ATTADIPA.**

Блокирующих exact trademark/software/wearable конфликтов в проведённом preliminary clearance не найдено. Из известных компромиссов принимаем:

1. существование индонезийской `PT Attadipa Elektro`;
2. занятый GitHub user `Attadipa`;
3. занятый X handle `@attadipa`;
4. буддийский informational search footprint;
5. необходимость отдельно подтвердить и купить домен через live registrar.

Переименование кода и репозитория можно готовить. **Публичный анонс и запуск сайта — после регистрации домена.**

### Источники clearance

- Значение `attadīpa`: https://www.wisdomlib.org/definition/attadipa
- Канонический текст SN 22.43: https://www.dhammatalks.net/suttacentral/sc2016/sc/sn22.43.html
- Перевод/контекст «island/refuge»: https://buddho.org/nl/wp-content/uploads/sites/2/Rahula_What-the-Buddha-Taught.pdf
- PT Attadipa Elektro, automation/instrumentation profile: https://repository.nscpolteksby.ac.id/154/7/7%20Bab%20IV.pdf
- PT Attadipa Elektro, telecom systems listing: https://buildeey.com/profile/attadipa-elektro-pt/en
- PT Attadipa Elektro, current contractor listing: https://indokontraktor.com/business/pt-attadipa-elektro-kota-surabaya
- attaDIPA Japan: https://attadipa.wixsite.com/heal
- Historical `attadipa.com` expiry/deletion listing: https://justdropped.com/drops/011915com.html
- X handle footprint `@attadipa`: https://twpro.jp/attadipa

> **Важно:** это engineering/brand preliminary clearance, а не юридическое заключение и не гарантия регистрации товарного знака.


---

## 1.2. Зафиксировать бренд внутри репозитория

Создать короткий канонический документ, например:

`docs/brand/naming.md`

Он должен содержать:

- [ ] `Attadipa` — имя проекта, всегда с такой капитализацией.
- [ ] `Lumar` — имя маскота.
- [ ] Lumar — светлячок; слово `firefly` допустимо только как обычное существительное.
- [ ] `Independent by design` — архитектурное кредо, раскрывающее смысл названия.
- [ ] Происхождение имени: палийское `attadīpa`; в самом бренде пишем `Attadipa` без диакритики.
- [ ] Attadipa не расшифровывается как аббревиатура.
- [ ] Не использовать `AttadipaOS` как альтернативное официальное имя без отдельного решения.
- [ ] Правила для `attadipa`, `ATTADIPA_`, `attadipa_*`.
- [ ] Русское написание в обычном тексте: **Аттадипа**, рекомендуемое произношение — **атта-ДИ-па**.
- [ ] В UI/логотипе бренд лучше оставлять латиницей: **Attadipa**.

Это станет защитой от постепенного расползания названия по коду.

---

# 2. Подготовка Git-миграции

## 2.1. Не переписывать историю Git

- [ ] **Не делать filter-repo/BFG по старым коммитам.**
- [ ] Не пытаться удалить `FireflyOS` из исторических commit messages.
- [ ] Не менять старые merge commits и PR history.

Причина: переписывание истории сломает SHA, PR references, forks и ссылки и ничего полезного для ребрендинга не даст.

Допустимо и полезно оставить историческую запись:

> Project formerly known as Firefly OS. Renamed to Attadipa in August 2026.

---

## 2.2. Делать rename отдельной атомарной работой

Рекомендуемый порядок:

- [ ] Создать ветку `rename/firefly-to-attadipa`.
- [ ] На время rename не принимать крупные архитектурные изменения.
- [ ] Не смешивать ребрендинг с рефакторингом логики.
- [ ] Сначала добиться зелёного host build + tests + simulator под новым именем.
- [ ] Затем слить rename.
- [ ] Сразу после merge переименовать сам GitHub repository.
- [ ] После rename репозитория провести второй проход проверки ссылок и внешних интеграций.

---

# 3. Полное переименование исходного кода

В текущем `main` старое имя уже является частью публичной технической поверхности, поэтому одной правки README недостаточно.

Во время аудита подтверждены, в частности:

- `project(firefly LANGUAGES CXX)`;
- `FIREFLY_BUILD_SIMULATOR`;
- `firefly_headers`;
- `firefly_platform`;
- `firefly_core`;
- `firefly_l10n`;
- `firefly_apps`;
- `firefly_sim`;
- CMake aliases `firefly::core`, `firefly::platform`, `firefly::apps`, `firefly::l10n`;
- `include/firefly/version.h`;
- `FIREFLY_VERSION_*`;
- `cmake/FireflyLvgl.cmake`;
- публичные include paths вида `firefly/...`.

Это значит, что rename должен быть **hard rename**, а не косметическим alias.

## 3.1. CMake

- [ ] `project(firefly ...)` → `project(attadipa ...)`.
- [ ] `FIREFLY_BUILD_SIMULATOR` → `ATTADIPA_BUILD_SIMULATOR`.
- [ ] `firefly_headers` → `attadipa_headers`.
- [ ] `firefly_platform` → `attadipa_platform`.
- [ ] `firefly_core` → `attadipa_core`.
- [ ] `firefly_l10n` → `attadipa_l10n`.
- [ ] `firefly_apps` → `attadipa_apps`.
- [ ] `firefly_sim` → `attadipa_sim`.
- [ ] `firefly::platform` → `attadipa::platform`.
- [ ] `firefly::core` → `attadipa::core`.
- [ ] `firefly::apps` → `attadipa::apps`.
- [ ] `firefly::l10n` → `attadipa::l10n`.
- [ ] `cmake/FireflyLvgl.cmake` → `cmake/AttadipaLvgl.cmake`.
- [ ] Обновить все `target_link_libraries`, `add_dependencies`, test targets и install/export rules.
- [ ] Проверить cache keys и CMake variables, в которых есть `firefly`.
- [ ] Удалить старые aliases после rename; проект пока достаточно молодой, чтобы не тащить вечный compatibility layer.

---

## 3.2. Public headers и namespaces

Переименовать деревья:

- [ ] `include/firefly/` → `include/attadipa/`.
- [ ] `core/include/firefly/` → `core/include/attadipa/`.
- [ ] `platform/include/firefly/` → `platform/include/attadipa/`.
- [ ] `apps/include/firefly/` → `apps/include/attadipa/`, если существует.
- [ ] `l10n/include/firefly/` → `l10n/include/attadipa/`, если существует.
- [ ] Аналогично для остальных компонентов.

В исходниках:

- [ ] `#include <firefly/...>` → `#include <attadipa/...>`.
- [ ] `namespace firefly` → `namespace attadipa`.
- [ ] `firefly::` → `attadipa::`.
- [ ] Include guards с `FIREFLY_` → `ATTADIPA_`.
- [ ] Макросы `FIREFLY_*` → `ATTADIPA_*`.
- [ ] Compile definitions `FIREFLY_*` → `ATTADIPA_*`.
- [ ] Test fixtures, mock namespaces и helper namespaces.
- [ ] Doxygen groups/namespaces, если есть.

---

## 3.3. Version API

Текущий:

`include/firefly/version.h`

Переехать должен в:

`include/attadipa/version.h`

И:

- [ ] `FIREFLY_VERSION_MAJOR` → `ATTADIPA_VERSION_MAJOR`.
- [ ] `FIREFLY_VERSION_MINOR` → `ATTADIPA_VERSION_MINOR`.
- [ ] `FIREFLY_VERSION_PATCH` → `ATTADIPA_VERSION_PATCH`.
- [ ] `FIREFLY_VERSION_STRING` → `ATTADIPA_VERSION_STRING`.
- [ ] `FIREFLY_STRINGIFY*` → `ATTADIPA_STRINGIFY*`.

**Версию из-за rename обнулять не нужно.** Rename — не новая кодовая база.

---

## 3.4. Simulator и CLI

- [ ] Бинарник `firefly_sim` → `attadipa_sim`.
- [ ] Usage/help/banner → Attadipa.
- [ ] Window title → Attadipa.
- [ ] Screenshot/output filenames → `attadipa-*`.
- [ ] Test snapshots/goldens, если имя входит в них.
- [ ] CLI examples в документации.
- [ ] Shell completion и launch configs.
- [ ] VS Code/CMake presets/tasks, если есть.
- [ ] Simulator build artifact names в CI.

---

# 4. Firmware/embedded identifiers — проверить даже если код ещё не дошёл до железа

Особенно важно не дать старому имени тихо попасть в протоколы и persistent storage.

По всему репозиторию проверить:

- [ ] BLE local name.
- [ ] BLE service/manufacturer data, если там кодируется бренд.
- [ ] USB product string.
- [ ] mDNS hostname.
- [ ] Wi-Fi AP/SSID по умолчанию.
- [ ] OTA product/channel identifiers.
- [ ] Firmware image filenames.
- [ ] Partition labels.
- [ ] NVS namespaces.
- [ ] Preferences/config namespaces.
- [ ] Persistent storage directory/key prefixes.
- [ ] Crash dump/report identifiers.
- [ ] Log tag prefixes.
- [ ] User-Agent строки.
- [ ] Mesh display name/default node name.
- [ ] Mesh protocol extension IDs, если они наши.
- [ ] Companion discovery/service names.
- [ ] Deep links/custom URI schemes.
- [ ] QR provisioning payloads.
- [ ] App IDs/package IDs будущего companion-приложения.
- [ ] Bluetooth appearance/product metadata.
- [ ] Serial console greeting.

### Правило

Если старый идентификатор **уже публиковался в совместимом протоколе или persistent storage**, сначала решить, нужна ли миграция.

Если он ещё нигде не выпущен пользователям — **переименовать жёстко сейчас**, без legacy aliases.

---

# 5. Tests, generators и tools

- [ ] Переименовать test targets с `firefly_*`.
- [ ] Обновить compile-fail/boundary fixtures.
- [ ] Обновить test include paths.
- [ ] Обновить generated file headers/comments.
- [ ] Проверить `tools/`.
- [ ] Проверить Python scripts на строки/пути `firefly`.
- [ ] Проверить localization generator.
- [ ] Проверить font/image pipeline.
- [ ] Проверить snapshot/golden paths.
- [ ] Проверить temporary output dirs.
- [ ] Проверить environment variables.
- [ ] Проверить `.gitignore` на старые build/output names.
- [ ] Проверить shell/PowerShell scripts.

---

# 6. GitHub Actions и автоматизация агентов

В репозитории сейчас есть отдельные workflows для CI и агентной очереди, поэтому этот раздел критический.

Проверить:

- `.github/workflows/ci.yml`
- `.github/workflows/agent-queue-watchdog.yml`
- `.github/workflows/claude-agent.yml`
- `.github/workflows/claude-ci-repair.yml`
- `.github/workflows/claude-pr-review.yml`
- `.github/workflows/codeql.yml`
- `.github/scripts/**`
- `.github/tests/**`
- `.github/dependabot.yml`

Для каждого:

- [ ] Hardcoded `FireflyOS`.
- [ ] Hardcoded `hleserg/FireflyOS`.
- [ ] `firefly` в artifact names.
- [ ] CMake target/binary names.
- [ ] Cache keys.
- [ ] Working directories.
- [ ] Prompt text для агентов.
- [ ] Названия продукта в комментариях/summary.
- [ ] Проверки regex/grep, ожидающие старые имена.
- [ ] Links в issue/PR comments.
- [ ] Repository dispatch payloads.
- [ ] Environment variables `FIREFLY_*`.

### GitHub secrets/variables — отдельно через Settings

Они не видны обычным grep репозитория:

- [ ] Actions Secrets.
- [ ] Actions Variables.
- [ ] Environment secrets/variables.
- [ ] Pages environment.
- [ ] Dependabot secrets.
- [ ] Repository webhooks.
- [ ] GitHub Apps/integration configuration.

Если secret называется, например, `FIREFLY_*`, его можно переименовать в `ATTADIPA_*`; сначала добавить новый, переключить workflow, убедиться в работе и только потом удалить старый.

---

# 7. Claude/agent instructions

Проверить полностью:

- [ ] `CLAUDE.md`.
- [ ] `.claude/**`.
- [ ] Все постоянные инструкции агента.
- [ ] Все prompts, где агенту дан URL `https://github.com/hleserg/FireflyOS`.
- [ ] Правила review/issue queue.
- [ ] Примеры команд.
- [ ] Тексты создаваемых Issues/PRs.
- [ ] Название продукта в system-like docs.

После rename агенты должны считать **`hleserg/Attadipa` единственной актуальной очередью/памятью проекта**.

---

# 8. Документация внутри репозитория

Провести полный текстовый проход по:

- [ ] `README.md`.
- [ ] `STATUS.md`.
- [ ] `TASKS.md`.
- [ ] `CONTRIBUTING.md`.
- [ ] `CLAUDE.md`.
- [ ] `docs/**`.
- [ ] ADR.
- [ ] research notes.
- [ ] hardware survey.
- [ ] UI/design docs.
- [ ] examples/tutorials.
- [ ] Mermaid/PlantUML diagrams.
- [ ] embedded links в Markdown.
- [ ] alt text изображений.
- [ ] badge URLs.
- [ ] clone/build commands.

### ADR

Старые ADR не нужно переписывать по смыслу, но брендовые названия и актуальные пути в них лучше обновить, если документ считается действующим.

Создать отдельное решение:

`docs/adr/XXXX-project-name-attadipa.md`

В нём зафиксировать:

- Firefly OS → Attadipa;
- дату;
- почему старое имя отброшено;
- что `Attadipa` — бренд платформы;
- что `Lumar` — mascot;
- что смысл имени Attadipa выражается архитектурным кредо `Independent by design`;
- что Git history не переписывается.

---

# 9. Формализовать смысл имени: Independent by design

Поскольку **Attadipa теперь является самим именем проекта**, отдельная сущность `Attadipa Principle` больше не нужна. Вместо неё фиксируем технически проверяемое кредо:

> **Independent by design:** core functionality that can reasonably execute on-device must not require a phone, cloud service, or persistent Internet connection merely for architectural convenience.

В отдельном ADR/Principles doc описать:

- [ ] что считается core functionality;
- [ ] когда remote provider допустим;
- [ ] phone/cloud — enhancement, а не обязательный brain;
- [ ] graceful degradation;
- [ ] локальный источник имеет понятный status/quality;
- [ ] отсутствие внешнего provider не должно превращать устройство в кирпич;
- [ ] offline-first решения предпочитаются там, где это разумно по ресурсам и UX;
- [ ] внешняя нода может расширять capability set, но сама платформа должна честно переживать её отсутствие.

В brand/naming doc отдельно указать, что это не произвольный маркетинговый слоган: он следует из палийского `attadīpa` — «опирающийся на себя / имеющий себя островом и прибежищем».

Это даёт названию реальный инженерный смысл, а не декоративную религиозную отсылку.


---

# 10. Localization и пользовательские строки

- [ ] Найти `Firefly`, `Firefly OS`, `FireflyOS` во всех EN/RU catalogues.
- [ ] Заменить бренд на `Attadipa`.
- [ ] Не переводить бренд как локализуемую строку без необходимости.
- [ ] В русском объяснительном тексте можно писать «Аттадипа», но логотип/название продукта — `Attadipa`.
- [ ] Firefly Node → Attadipa Node.
- [ ] Обновить boot/splash screen.
- [ ] About/System info.
- [ ] Error/report text.
- [ ] Simulator diagnostic screen.
- [ ] Accessibility labels.
- [ ] Screenshot/reference UI.

---

# 11. Брендинг и графические файлы

Существующие визуальные материалы сделаны под Firefly OS, поэтому нужно отличать **маскота** от **wordmark**.

## Можно сохранить

- [ ] Самого светлячка.
- [ ] Тёплую палитру.
- [ ] Основной визуальный язык.
- [ ] Glow / guide / connect как идею, если она всё ещё нравится.

## Нужно переделать

- [ ] Wordmark `Firefly OS` → `Attadipa`.
- [ ] Banner.
- [ ] README hero/banner.
- [ ] Social preview.
- [ ] OpenGraph image.
- [ ] GitHub social preview.
- [ ] Website hero.
- [ ] Favicon/icon, если в нём есть буква/слово Firefly.
- [ ] Design boards, где старое название встроено в изображение.
- [ ] Screenshots интерфейса, где видно Firefly.
- [ ] SVG metadata/title/desc.
- [ ] Image filenames по возможности.
- [ ] `pics/README.md`.

### Lumar

Отдельно добавить маленькую brand note:

> Lumar is Attadipa’s firefly mascot.

Не превращать `Lumar` в название firmware namespace: персонаж и платформа должны оставаться разными сущностями.

---

# 12. Сайт

Локальный презентационный сайт сейчас также является частью rename и требует полноценного прохода.

## Текст

- [ ] Firefly OS → Attadipa.
- [ ] Firefly → Attadipa там, где это бренд.
- [ ] Firefly Node → Attadipa Node.
- [ ] Добавить имя Lumar к mascot section.
- [ ] При желании добавить короткую секцию про `Independent by design` и происхождение имени Attadipa.
- [ ] Origin story обновить, чтобы она рассказывала уже о Attadipa.

## HTML/meta/SEO

- [ ] `<title>`.
- [ ] meta description.
- [ ] OpenGraph title/description.
- [ ] Twitter/social metadata.
- [ ] JSON-LD, если появится.
- [ ] canonical URL.
- [ ] `manifest.webmanifest` `name` / `short_name`.
- [ ] theme/app metadata.
- [ ] alt texts.
- [ ] sitemap.
- [ ] robots.
- [ ] 404 page.

## JavaScript/storage

- [ ] `firefly-site-lang` → `attadipa-site-lang`.
- [ ] Остальные storage keys.
- [ ] Query params/analytics event names, если есть.
- [ ] JS constants с названием проекта.

## GitHub links

Все ссылки:

`https://github.com/hleserg/FireflyOS/...`

→

`https://github.com/hleserg/Attadipa/...`

Включая:

- README;
- STATUS;
- TASKS;
- Discussions;
- docs/adr;
- research;
- source links.

## GitHub Pages

Если сайт публикуется как project Pages:

`https://hleserg.github.io/FireflyOS/`

после rename должен стать:

`https://hleserg.github.io/Attadipa/`

Поэтому:

- [ ] обновить canonical;
- [ ] OG URL;
- [ ] sitemap;
- [ ] абсолютные asset paths;
- [ ] Pages deployment workflow;
- [ ] проверить Settings → Pages;
- [ ] вручную открыть все основные URL после deployment.

### Лучше

После регистрации собственного домена подключить его к Pages. Тогда будущие rename репозитория не будут менять публичный URL сайта.

---

# 13. GitHub repository rename

Когда новый `main` уже готов:

**Settings → General → Repository name**

`FireflyOS` → `Attadipa`

После этого:

- [ ] Проверить `https://github.com/hleserg/Attadipa`.
- [ ] Проверить redirect со старого URL.
- [ ] Не создавать новый репозиторий `hleserg/FireflyOS`: это может уничтожить полезность redirect старых ссылок.
- [ ] Обновить repository description.
- [ ] Обновить homepage URL.
- [ ] Обновить topics при необходимости.
- [ ] Заменить social preview.
- [ ] Проверить Discussions.
- [ ] Проверить Issues.
- [ ] Проверить Actions.
- [ ] Проверить branch protection/rulesets.
- [ ] Проверить environments.
- [ ] Проверить Pages.
- [ ] Проверить CodeQL/security settings.
- [ ] Проверить Dependabot.
- [ ] Проверить webhooks/GitHub Apps.

GitHub обычно поддерживает redirect старого repository URL после rename, но **на redirects нельзя строить новую документацию**: все наши актуальные ссылки должны быть заменены на новый URL.

---

# 14. Issues, PRs и Discussions

## Не нужно

- Переписывать номера Issues.
- Переписывать номера PR.
- Закрывать и пересоздавать Issues.
- Переписывать Git history.

## Нужно

- [ ] Переименовать GitHub issue/discussion templates.
- [ ] Обновить pinned Discussions.
- [ ] Обновить welcome/community тексты.
- [ ] Старые активные Issues с `FireflyOS` в текущем техническом контексте — исправить.
- [ ] Исторические закрытые Issues можно не чистить массово.
- [ ] Активные PR bodies/comments с абсолютными old-repo URLs — обновить при необходимости.
- [ ] Обновить bot/agent generated text.

Добавить pinned/announcement discussion:

> **Firefly OS is now Attadipa**

Коротко объяснить, что изменилось только имя/бренд, а не цели проекта и история Git.

---

# 15. Releases, artifacts и package names

Проверить:

- [ ] GitHub Releases.
- [ ] Release asset filenames.
- [ ] Actions artifacts.
- [ ] Firmware ZIP/bin names.
- [ ] Docker images, если появятся.
- [ ] GHCR packages.
- [ ] PlatformIO packages.
- [ ] ESP Component Registry.
- [ ] Package metadata.
- [ ] SBOM/project name.
- [ ] Code signing metadata.
- [ ] OTA manifest.

Новые artifacts должны называться, например:

`attadipa-0.1.0-...`

а не `firefly-*`.

Старые опубликованные release assets обычно лучше оставить историческими, чем ломать ссылки. Если проект ещё не выпускал реальные публичные версии — можно провести чистый cutover.

---

# 16. Внешние сервисы и интеграции

Проверить всё, что может хранить repository slug или имя проекта:

- [ ] Codecov.
- [ ] Sentry.
- [ ] Sonar/CodeQL external dashboards.
- [ ] Renovate/Dependabot external config.
- [ ] CI outside GitHub.
- [ ] Docker Hub/GHCR.
- [ ] Cloudflare.
- [ ] DNS.
- [ ] GitHub Pages custom domain.
- [ ] Uptime monitoring.
- [ ] Search Console/webmaster tools.
- [ ] Analytics.
- [ ] Discord/Telegram bots.
- [ ] Webhooks.
- [ ] Notion/Linear links, если проект там упоминается.
- [ ] Any API tokens/scopes tied to exact repo path.

---

# 17. Локальные копии и рабочие окружения

На каждом компьютере/WSL/сервере/агенте:

```bash
git remote set-url origin https://github.com/hleserg/Attadipa.git
git remote -v
```

По желанию переименовать локальную папку:

```bash
mv FireflyOS Attadipa
```

Также проверить:

- [ ] VS Code workspace.
- [ ] CLion/CMake profiles.
- [ ] WSL scripts.
- [ ] Docker bind mounts/volume names.
- [ ] shell aliases.
- [ ] PowerShell scripts.
- [ ] local build directories.
- [ ] Claude Code project/worktree paths.
- [ ] cron jobs.
- [ ] scheduled scripts.
- [ ] local bookmarks.
- [ ] browser bookmarks.
- [ ] saved terminal profiles.

### Build dirs

После массового CMake rename лучше удалить старые build trees и пересобрать с нуля, чтобы cache не маскировал ошибки:

```bash
rm -rf build build-sim
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-sim -DATTADIPA_BUILD_SIMULATOR=ON
cmake --build build-sim
ctest --test-dir build-sim --output-on-failure
```

---

# 18. ChatGPT/Claude/прочие регулярные задачи

Отдельно проверить все scheduled/recurrent agent tasks, которые содержат:

- `FireflyOS`;
- `github.com/hleserg/FireflyOS`;
- «Firefly upstream review»;
- старые имена веток/артефактов.

Нужно обновить:

- [ ] регулярные code review routines;
- [ ] weekly upstream research;
- [ ] prompts для Claude;
- [ ] prompts для ChatGPT;
- [ ] любые cron/agent watchers;
- [ ] automation, создающую Issues;
- [ ] monitoring CI/PR/repository events.

После rename сделать **тестовый полный цикл**:

1. создать тестовую задачу;
2. убедиться, что агент видит `hleserg/Attadipa`;
3. агент создаёт/обрабатывает Issue;
4. PR/commit попадает в правильный repo;
5. CI запускается;
6. watchdog не считает очередь сломанной.

---

# 19. Поиск всех старых упоминаний

До merge выполнить минимум:

```bash
git grep -nEi 'FireflyOS|Firefly OS|Firefly|firefly|FIREFLY'
```

Отдельно проверить имена файлов/директорий:

```bash
find . -depth \( -iname '*firefly*' -o -iname '*Firefly*' \) -print
```

Если установлен ripgrep:

```bash
rg -n --hidden \
  --glob '!.git/**' \
  'FireflyOS|Firefly OS|Firefly|firefly|FIREFLY'
```

После rename повторить.

### Каждый оставшийся результат должен попасть в одну из трёх категорий

1. **Ошибка rename** → исправить.
2. **Историческая migration note** → оставить.
3. **Обычное слово firefly про Lumar как насекомое** → оставить осознанно.

Не должно быть результатов категории «ну вроде никому не мешает».

---

# 20. Проверки после технического rename

## Host build

- [ ] clean configure;
- [ ] clean build;
- [ ] все unit tests;
- [ ] boundary/compile-fail tests;
- [ ] generated sources reproducible.

## Simulator

- [ ] build с `ATTADIPA_BUILD_SIMULATOR=ON`;
- [ ] запускается `attadipa_sim`;
- [ ] обе поддерживаемые геометрии;
- [ ] EN;
- [ ] RU;
- [ ] screenshots;
- [ ] UI нигде не показывает Firefly как бренд.

## CI

- [ ] CI green.
- [ ] CodeQL green.
- [ ] agent workflows green.
- [ ] watchdog green.
- [ ] CI repair workflow не ссылается на старый repo.
- [ ] PR review workflow работает.

## GitHub

- [ ] clone нового URL.
- [ ] redirect старого URL.
- [ ] Issues.
- [ ] Discussions.
- [ ] Actions.
- [ ] Pages.
- [ ] source links.
- [ ] badges.

## Website

- [ ] desktop EN.
- [ ] desktop RU.
- [ ] mobile EN.
- [ ] mobile RU.
- [ ] favicon.
- [ ] OG preview.
- [ ] canonical.
- [ ] sitemap.
- [ ] все GitHub links.

---

# 21. Публичный cutover

Когда всё выше зелёное:

- [ ] Обновить README.
- [ ] Выложить новый сайт.
- [ ] Поменять social preview.
- [ ] Опубликовать rename announcement.
- [ ] Закрепить announcement в Discussions.
- [ ] Обновить Telegram/community descriptions, если есть.
- [ ] Обновить ссылки в личном профиле/README, если проект там указан.
- [ ] Обновить поисковые описания/метаданные.
- [ ] При наличии собственного домена — сделать его каноническим.

Короткая формулировка:

> **Firefly OS is now Attadipa.**  
> Same project, same goals, new name. Lumar remains our firefly mascot.

---

# 22. Через 1–2 недели после rename

- [ ] Google/Bing search `FireflyOS hleserg` — убедиться, что старые результаты ведут на новый repo/site.
- [ ] Search `Attadipa ESP32`.
- [ ] Search `Attadipa wearable`.
- [ ] Проверить broken links.
- [ ] Проверить GitHub traffic/referrers.
- [ ] Проверить старые bookmarks/redirect.
- [ ] Проверить scheduled agent tasks.
- [ ] Проверить incoming Issues: не путаются ли люди в именах.
- [ ] Повторить `rg` по repo.
- [ ] Удалить временные compatibility variables/secrets, если использовались.
- [ ] Удалить старые CI caches, если они больше не нужны.

---

# 23. Что специально НЕ делать

- [ ] **Не** переписывать Git history ради удаления слова Firefly.
- [ ] **Не** оставлять одновременно официальные `FireflyOS` и `Attadipa`.
- [ ] **Не** называть новый repo `AttadipaOS`, если бренд зафиксирован как Attadipa.
- [ ] **Не** сохранять `firefly::*` aliases «на всякий случай», если ни один публичный downstream от них ещё не зависит.
- [ ] **Не** оставлять старые `FIREFLY_*` env/macros рядом с новыми без migration reason.
- [ ] **Не** полагаться на GitHub redirect как на постоянный способ поддерживать наши собственные ссылки.
- [ ] **Не** создавать новый `hleserg/FireflyOS` после rename.
- [ ] **Не** удалять слово `firefly`, если оно честно описывает Lumar как насекомое.
- [ ] **Не** смешивать rename с функциональным рефакторингом.

---

# 24. Рекомендуемый порядок исполнения — короткая версия

## Phase A — Reserve

1. Final trademark/product clearance.
2. Register domain.
3. Reserve handles/namespaces.
4. Зафиксировать naming rules.

## Phase B — Code rename

5. `firefly` → `attadipa` в namespaces/includes/macros.
6. CMake targets/options/binary.
7. Tests/tools/generators.
8. Clean build + simulator + CI.

## Phase C — Docs & brand

9. README/STATUS/TASKS/CLAUDE/docs.
10. ADR о rename.
11. ADR/Principles: `Independent by design` + происхождение имени Attadipa.
12. Brand assets → Attadipa.
13. Lumar получает имя в документации.
14. Website → Attadipa.

## Phase D — GitHub cutover

15. Merge rename branch.
16. Rename repository `FireflyOS` → `Attadipa`.
17. Update Settings/Pages/social preview/integrations.
18. Validate Issues/Discussions/Actions/agents.

## Phase E — External cutover

19. Domain/DNS/site.
20. Scheduled ChatGPT/Claude tasks.
21. External integrations.
22. Local clones/remotes.
23. Announcement.

## Phase F — Audit

24. Full grep.
25. Broken-link scan.
26. Build/test/CI.
27. Browser/mobile/site.
28. Agent end-to-end test.
29. Повторная проверка через 1–2 недели.

---

# 25. Definition of Done

Переименование считается завершённым только когда одновременно выполняется всё ниже:

- [ ] Основной GitHub repo называется `hleserg/Attadipa`.
- [ ] Host build чисто собирается с новым namespace/targets.
- [ ] `attadipa_sim` собирается и работает.
- [ ] Нет брендовых `FireflyOS`, `Firefly OS`, `firefly::*`, `FIREFLY_*`.
- [ ] Остаточные `firefly` — только осознанное описание Lumar или историческая note.
- [ ] Все GitHub Actions зелёные.
- [ ] Claude/agent queue работает на новом repo.
- [ ] README/docs/status/tasks используют Attadipa.
- [ ] Сайт называется Attadipa и все URL правильные.
- [ ] Визуальные assets не содержат Firefly OS.
- [ ] GitHub Pages/custom domain работает.
- [ ] Lumar формально указан как mascot Attadipa.
- [ ] `Independent by design` документирован и связан с происхождением имени Attadipa.
- [ ] Все локальные/external integrations переведены.
- [ ] Старый GitHub URL корректно редиректит.
- [ ] Есть публичная короткая запись о rename.
- [ ] Финальный `rg` не показывает забытых старых технических идентификаторов.
