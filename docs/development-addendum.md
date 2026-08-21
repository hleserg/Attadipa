> # ⛔ SUPERSEDED — 2026-08-21
>
> **Этот документ больше не является действующей спецификацией.**
> Действующая — [`master-prompt-final.md`](master-prompt-final.md).
>
> This file is **history**, kept for the same reason as
> [`master-prompt.md`](master-prompt.md): ADRs and the reuse ledger quote it.
>
> Its content was not dropped — it was absorbed. The reuse-first rule is final
> §64–§67, the lookahead research pipeline is final §68, latency hiding is
> final §69, subagent discipline is final §70, task management is final §71,
> and "continue while work exists" is final §72. Where the wording differs, the
> final prompt wins.
>
> Two of this document's rules were followed loosely enough that the owner
> called it out on 2026-08-21, and the final prompt now states them with teeth:
> the reuse ledger must contain records rather than a template (final §67), and
> task/status consistency is a deliverable checked in the same commit as the
> change (final §73).
>
> **Do not fix anything in this file.**

---

# ДОПОЛНЕНИЕ: REUSE-FIRST DEVELOPMENT, LOOKAHEAD RESEARCH И CONTINUOUS EXECUTION

## REUSE FIRST — НЕ ИЗОБРЕТАЙ УЖЕ РЕШЁННОЕ

Firefly не должен превращаться в проект, где мы заново реализуем всё только потому, что можем.

Перед реализацией **каждого нового существенного subsystem / feature / driver / algorithm / UI pattern** сначала исследуй существующие open-source реализации.

Это постоянное правило разработки, а не одноразовый Research Phase.

Перед началом нового направления задай вопрос:

> Кто уже решал эту задачу хорошо, на похожем железе и в похожих ограничениях?

И только после этого решай, что делать:

- использовать готовый компонент;
- адаптировать его;
- обернуть Firefly API вокруг него;
- взять отдельный алгоритм;
- взять архитектурную идею;
- upstream'ить необходимые изменения;
- или написать собственную реализацию, если существующие решения объективно не подходят.

---

# OPEN-SOURCE RECONNAISSANCE ПЕРЕД КАЖДЫМ БОЛЬШИМ FEATURE

Прежде чем серьёзно писать новый компонент, проведи целевое исследование существующих проектов.

Это относится, например, к:

- MeshCore;
- Meshtastic;
- Ripple/MCLite и другим wearable/mesh UI;
- ESP32 watch firmware;
- PineTime/InfiniTime;
- Zephyr wearable projects;
- LVGL-based watches;
- Garmin-like open projects;
- GNSS libraries;
- u-blox integrations;
- compass implementations;
- magnetometer calibration;
- AHRS/sensor fusion;
- navigation;
- power management;
- LoRa coexistence;
- BLE companion protocols;
- Android notification bridges;
- notification UI;
- watchface frameworks;
- input methods;
- haptic systems;
- OTA;
- filesystem/storage;
- settings frameworks;
- logging;
- diagnostics;
- embedded event buses;
- simulator architectures;
- testing infrastructure.

Не ограничивайся этим списком.

Для конкретной задачи ищи именно проекты, которые уже решали **эту конкретную проблему**.

---

# ИССЛЕДОВАТЬ НЕ ТОЛЬКО «НАШУ» ЭКОСИСТЕМУ

Не зацикливайся на ESP32 или MeshCore.

Хорошие решения могут находиться в совершенно других проектах.

Например:

для power management можно изучить:

- ESP-IDF проекты;
- Zephyr;
- smartwatch firmware;
- sensor nodes.

Для compass:

- outdoor navigation projects;
- drones;
- flight controllers;
- robotics;
- AHRS libraries;
- hiking devices.

Для Child Mode:

- wearable interfaces;
- детские smart watches;
- game UI;
- accessibility-oriented interfaces.

Для notification architecture:

- Wear OS;
- Gadgetbridge-supported devices;
- open smartwatch firmware;
- BLE notification implementations.

Для coexistence:

- RF devices;
- flight controllers;
- embedded sensor platforms;
- SDR/LoRa projects.

Ищи **решённую инженерную задачу**, а не только проект с похожим названием.

---

# НЕ КОПИРУЙ ПЕРВУЮ НАЙДЕННУЮ РЕАЛИЗАЦИЮ

Existing open-source ≠ automatically good.

Перед reuse оценивай:

```text
Correctness
Maturity
Real-world usage
Testing
Maintenance status
Code quality
License
Memory usage
CPU usage
Power implications
Dependencies
ESP-IDF compatibility
Hardware assumptions
Portability
Thread safety
Security implications
Upstream activity
```

Особенно ценны:

- зрелые проекты;
- код, который реально годами используется на устройствах;
- реализации с hardware testing;
- unit/integration tests;
- active upstream;
- известные пользователи проекта;
- хорошо документированные решения.

GitHub stars сами по себе доказательством качества не являются.

---

# REUSE DECISION

Для каждого существенного внешнего решения зафиксируй одно из:

```text
USE AS-IS
USE AS DEPENDENCY
WRAP
PORT
ADAPT
EXTRACT ALGORITHM
INSPIRE ARCHITECTURE
UPSTREAM PATCH
REIMPLEMENT
REJECT
```

И кратко объясни почему.

Если принято решение `REIMPLEMENT`, должен существовать ответ на вопрос:

> Почему существующие проверенные реализации нам не подходят?

«Проще написать самому» без анализа — недостаточная причина.

---

# REUSE LEDGER

Поддерживай:

`docs/research/REUSE_LEDGER.md`

Пример записи:

```text
Problem:
Magnetometer calibration

Projects investigated:
- project A
- project B
- project C

Useful implementation:
project B / module X

License:
...

Strengths:
...

Weaknesses:
...

Decision:
EXTRACT ALGORITHM / ADAPT

Reason:
...

Source revision:
commit/tag

Firefly integration:
...

Tests required:
...
```

Это позволит следующему разработчику понять не только **что** было выбрано, но и **почему**.

---

# ОБЯЗАТЕЛЬНО ФИКСИРУЙ ТОЧНУЮ ВЕРСИЮ ИСТОЧНИКА

Если используешь чужой код или архитектуру, фиксируй:

- repository;
- project;
- tag/version;
- commit hash;
- relevant source files;
- license.

Не писать в документации просто:

> взято из Meshtastic

или:

> аналогично Zephyr.

Нужно иметь возможность вернуться к конкретной реализации.

---

# НЕ СОЗДАВАЙ БЕСКОНЕЧНЫЙ FORK

При интеграции внешних проектов предпочитать:

```text
upstream dependency
        +
thin Firefly wrapper
```

вместо:

```text
copy entire source tree
        +
modify everything
        +
never update again
```

Если изменения upstream dependency неизбежны:

1. держать patch минимальным;
2. документировать его;
3. по возможности сделать его пригодным для upstream;
4. следить, нельзя ли позже вернуться к чистому upstream.

---

# LOOKAHEAD RESEARCH PIPELINE

Исследование должно идти **впереди разработки**, а не тормозить её перед каждой задачей.

Используй модель:

```text
CURRENT
NEXT
AFTER NEXT
```

Например:

```text
CURRENT:
реализуется display/touch BSP

NEXT:
GNSS integration

AFTER NEXT:
MeshCore integration
```

Пока основной поток реализует `CURRENT`, отдельный research/subagent уже исследует `NEXT`.

Если ресурсов достаточно — другой исследователь может подготовить `AFTER NEXT`.

Цель:

> Когда текущая задача закончена, следующая уже должна иметь большую часть необходимых исследований, источников и решений.

---

# RESEARCH НЕ ДОЛЖЕН БЫТЬ НА КРИТИЧЕСКОМ ПУТИ БЕЗ НЕОБХОДИМОСТИ

Плохо:

```text
закончить Feature A
↓
только после этого начать искать информацию по Feature B
↓
ждать
↓
читать
↓
сравнивать
↓
только потом начать Feature B
```

Предпочтительно:

```text
Implementation A ────────────────┐
                                 ↓
Research B ───────────────────── ready B
Research C ──────────────┐
                         ↓
                     preliminary C
```

Используй subagents для такого lookahead там, где среда это поддерживает.

---

# НЕ ЗАБЕГАЙ СЛИШКОМ ДАЛЕКО

Lookahead должен уменьшать простои, а не создавать гору устаревших исследований.

Обычно достаточно исследовать:

- следующую задачу подробно;
- ещё одну следующую предварительно.

Не проводить глубокое исследование Feature №27, если архитектура Feature №3 ещё может полностью изменить требования.

---

# PREFLIGHT ДЛЯ СЛЕДУЮЩЕЙ ЗАДАЧИ

Для `NEXT` заранее выяснить хотя бы:

```text
dependencies
upstream projects
hardware facts
likely integration API
licensing
known pitfalls
tests
hardware requirements
major architectural risks
```

Так следующий этап сможет стартовать практически сразу.

---

# ПРЕДВАРИТЕЛЬНО ЗАПУСКАЙ ДОЛГИЕ ОПЕРАЦИИ

Постоянно ищи операции, которые могут позже остановить разработку на значительное время.

Если их можно безопасно начать заранее — начинай.

Например:

- dependency download;
- repository cloning;
- toolchain installation;
- ESP-IDF setup;
- compilation больших dependencies;
- native toolchain setup;
- simulator dependencies;
- initial CI run;
- static analysis;
- large test suites;
- documentation indexing;
- repository inspection;
- subagent research;
- reference build;
- generation of test assets.

Не жди момента, когда результат станет критически необходим.

---

# LATENCY HIDING

Используй время эффективно.

Если идёт длительная операция:

```text
build
test
download
analysis
subagent research
```

и она не требует твоего постоянного вмешательства, продолжай независимую работу.

Например:

```text
CI/build running
        +
write tests
        +
update docs
        +
review NEXT research
```

или:

```text
subagent investigates GNSS
        +
main agent implements UI
```

Не простаивай без причины.

---

# НО НЕ СОЗДАВАЙ СПЕКУЛЯТИВНЫЙ МУСОР

Не запускай заранее всё подряд.

Предварительная работа должна иметь высокую вероятность использования.

Перед длинной speculative operation спроси:

> Скорее всего это понадобится в ближайших 1–3 шагах?

Если нет — отложи.

---

# PARALLELIZE READS, SERIALIZE FOUNDATIONAL WRITES

Хорошо параллелить:

- исследования;
- чтение репозиториев;
- анализ datasheet;
- поиск existing implementations;
- benchmark исследования;
- test design;
- documentation research.

Осторожно параллелить изменения:

- Core API;
- CMake/build architecture;
- event model;
- BSP interfaces;
- shared headers;
- storage format;
- protocol schema.

Несколько агентов не должны одновременно независимо менять фундамент проекта.

---

# CONTINUOUS WORK QUEUE

Поддерживай живой рабочий список.

Минимум:

`TASKS.md`

или эквивалентный механизм среды разработки.

Структура:

```text
NOW

NEXT

READY

BLOCKED

WAITING

DONE
```

Для задач указывать:

```text
priority
dependencies
acceptance criteria
owner/subagent
research status
implementation status
tests
hardware required
```

---

# NOW ДОЛЖЕН БЫТЬ МАЛЕНЬКИМ

Не держать 17 задач одновременно в `NOW`.

Обычно:

- одна основная implementation task;
- одна-две независимые вспомогательные;
- несколько параллельных research tasks.

Так сохраняется фокус.

---

# READY ОЗНАЧАЕТ ДЕЙСТВИТЕЛЬНО READY

Задача попадает в `READY`, когда:

- зависимости понятны;
- критичные исследования сделаны;
- нет известного blocker;
- acceptance criteria определены.

Это позволяет брать следующую работу без нового длительного planning cycle.

---

# ПРАВИЛО: НЕ ОСТАНАВЛИВАЙСЯ ПОСЛЕ ОДНОЙ ЗАДАЧИ

Не воспринимай завершение отдельного пункта как повод прекратить работу.

После завершения:

1. запусти tests;
2. обнови docs/status;
3. закрой задачу;
4. посмотри `READY`;
5. возьми следующую наиболее приоритетную незаблокированную задачу;
6. продолжай.

Не останавливайся только для того, чтобы сообщить:

> Я закончил первый этап, хотите продолжить?

Если в задании уже ясно, что проект надо продолжать — **продолжай**.

---

# НЕ ОСТАНАВЛИВАЙСЯ ИЗ-ЗА ЛОКАЛЬНОГО BLOCKER

Если задача заблокирована, а другая полезная работа возможна:

```text
mark BLOCKED
↓
record reason
↓
select next READY task
↓
continue
```

Примеры:

нет реальной платы →

- продолжить simulator;
- tests;
- documentation;
- protocol;
- compile validation;
- hardware test tooling.

Нет подтверждённого pinout →

- не выдумывать pinout;
- переключиться на независимую задачу.

---

# КОГДА МОЖНО ОСТАНОВИТЬСЯ

Рабочий цикл можно завершить, если выполняется хотя бы одно:

1. Все доступные задачи данного scope выполнены.
2. Все оставшиеся задачи реально заблокированы внешними условиями.
3. Требуется решение пользователя, которое нельзя безопасно принять самостоятельно.
4. Требуется необратимая операция.
5. Требуется физическое действие пользователя.
6. Закончились доступные ресурсы/время execution environment.

Просто:

> Следующий пункт сложный.

не является причиной остановки.

---

# НЕ СПРАШИВАЙ РАЗРЕШЕНИЯ НА ОЧЕВИДНОЕ ПРОДОЛЖЕНИЕ

Если следующий шаг однозначно следует из master plan — выполняй его.

Не надо постоянно спрашивать:

> Перейти к следующему файлу?

> Запустить тесты?

> Обновить документацию?

> Исследовать следующую библиотеку?

Это нормальная часть твоей работы.

Разрешение нужно для действий, которые:

- необратимы;
- опасны;
- требуют product decision;
- требуют credentials;
- затрагивают внешние системы существенным образом.

---

# TASK → TEST → DOCUMENT → NEXT

Используй постоянный цикл:

```text
Research enough
↓
Implement
↓
Build
↓
Test
↓
Inspect result
↓
Fix
↓
Document
↓
Update task state
↓
Start NEXT
```

Не оставляй tests/documentation «на потом» огромным хвостом.

---

# RESEARCH → IMPLEMENTATION HANDOFF

Research agent не должен заканчивать ответом на 20 страниц текста без actionable result.

Его результат должен содержать:

```text
Recommended solution

Best reusable projects/components

Exact repositories/commits

Relevant source files

Compatibility concerns

License

Known issues

Suggested Firefly API

Tests to port/reproduce

Questions still unresolved
```

Главный агент должен иметь возможность сразу использовать результат.

---

# PORT TESTS TOGETHER WITH CODE

Если reuse'ишь существующий компонент или алгоритм, ищи его tests.

Очень желательно переносить или адаптировать:

- reference vectors;
- unit tests;
- integration scenarios;
- known edge cases;
- hardware test procedures.

Готовый проверенный алгоритм без его проверок менее ценен.

---

# ИЩИ НЕ ТОЛЬКО КОД, НО И НАКОПЛЕННЫЕ ОШИБКИ

При исследовании upstream изучай не только исходники.

Смотри:

- issues;
- pull requests;
- discussions;
- changelog;
- bug fixes;
- hardware-specific reports.

Особенно полезны закрытые bugs, потому что они показывают:

> Какие очевидные на первый взгляд решения уже ломались у других людей.

Мы хотим наследовать чужой опыт, а не только чужой код.

---

# ИСПОЛЬЗУЙ ПРОВЕРЕННЫЕ АЛГОРИТМЫ

Особенно не изобретай с нуля без необходимости:

- magnetometer calibration;
- sensor fusion;
- coordinate math;
- geodesic calculations;
- filtering;
- GNSS parsing;
- cryptography;
- mesh routing concepts;
- packet replay protection;
- battery estimation;
- time synchronization.

Если существует зрелое, проверенное решение с подходящей лицензией и ресурсными требованиями — предпочесть его.

---

# FIRELFY-SPECIFIC VALUE ДОЛЖНА БЫТЬ В ИНТЕГРАЦИИ

Мы не обязаны быть авторами каждой математической функции.

Ценность Firefly состоит в том, что мы хорошо объединяем:

```text
wearable UX
+
MeshCore
+
navigation
+
low power
+
hardware coexistence
+
adult/child UI
+
companion integration
```

Поэтому переиспользование качественных фундаментальных компонентов — достоинство, а не недостаток.

---

# BUILD VS BUY VS ADAPT ДЛЯ OPEN SOURCE

Для каждого крупного subsystem фактически делай мини-решение:

```text
BUILD
ADAPT
REUSE
```

Предпочтительный порядок:

```text
REUSE
↓
ADAPT
↓
BUILD
```

если нет веских причин наоборот.

---

# PROACTIVE BOTTLENECK REVIEW

Периодически, например после каждого milestone, посмотри на ближайший roadmap и спроси:

> Что с высокой вероятностью станет следующим большим тормозом?

Например:

- неизвестная библиотека;
- сложный toolchain;
- hardware documentation;
- лицензирование;
- MeshCore integration;
- большой compile dependency;
- Android API restrictions;
- sensor calibration;
- required test infrastructure.

Если можно снять этот риск заранее дешёвой исследовательской задачей — сделай это сейчас.

---

# KEEP THE PIPELINE FULL

В идеальном состоянии проекта одновременно существуют:

```text
CURRENT implementation

NEXT task researched and READY

AFTER NEXT preliminary research running

tests validating recently completed work

CI validating repository
```

Не обязательно буквально всегда иметь пять параллельных процессов.

Это принцип организации работы:

**минимизировать время, когда главный development flow вынужден ждать.**

---

# STATUS ДОЛЖЕН ПОКАЗЫВАТЬ НЕ ТОЛЬКО ПРОШЛОЕ, НО И БУДУЩЕЕ

В `STATUS.md` добавить:

```text
CURRENT

NEXT READY

LOOKAHEAD RESEARCH

LONG-RUNNING OPERATIONS

BLOCKED

RECENTLY COMPLETED
```

Так другой агент или новая сессия сразу понимает не только, где работа остановилась, но и что уже подготовлено дальше.

---

# ПРАВИЛО НЕПРЕРЫВНОГО ДВИЖЕНИЯ

Главный принцип работы:

> Пока существует полезная, безопасная, незаблокированная работа в рамках поставленного проекта — продолжай работать.

Не ограничивай себя минимальным формальным выполнением ближайшего пункта.

Но также:

> Не создавай работу ради работы.

Каждый следующий шаг должен приближать Firefly к проверяемому работающему продукту.

---

# ИТОГОВЫЙ DEVELOPMENT PIPELINE

Стремись к состоянию:

```text
          ┌── Research NEXT
          │
          ├── Research AFTER NEXT
          │
CURRENT IMPLEMENTATION
          │
          ├── Tests / CI
          │
          └── Documentation
```

После завершения CURRENT:

```text
NEXT → CURRENT
AFTER NEXT → NEXT
new research → AFTER NEXT
```

То есть разработка Firefly должна работать как непрерывный конвейер, а не как последовательность:

```text
думать
останавливаться
искать
ждать
писать
останавливаться
думать заново
```

---

# КРАТКАЯ ФОРМУЛА

**Research ahead.  
Reuse proven work.  
Hide latency.  
Keep tasks ready.  
Never guess hardware.  
Never reinvent mature solutions without reason.  
Test what you reuse.  
Document why.  
If one task blocks, move to another.  
While useful work remains, keep going.**