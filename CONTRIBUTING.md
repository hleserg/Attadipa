# Contributing to Attadipa

## Ideas go to Discussions. Issues are the engineering queue.

**Have an idea? [Start a Discussion](https://github.com/hleserg/Attadipa/discussions).**
If it turns out to be something worth building, a maintainer will turn it into
an Issue.

Issues are Attadipa's executable engineering queue — the working memory that
coding agents and maintainers pick tasks from, in a defined format, with a
lifecycle attached. Discussions are where ideas are argued about until they are
good enough to enter that queue. Filing a half-formed idea as an Issue does not
speed it up; it just puts an unanswerable question into a queue that expects
answerable ones.

That is why issue creation is limited to collaborators. It is not a wall around
the project — Discussions are open to everybody, and they are the front door.

---

**Есть идея? [Начните Discussion](https://github.com/hleserg/Attadipa/discussions).**
Если из неё получается конкретная работа, maintainer перенесёт её в Issue.

Issues — исполняемая инженерная очередь Attadipa: рабочая память, из которой
берут задачи агенты и мейнтейнеры, в заданном формате и с понятным жизненным
циклом. Discussions — место, где идеи обсуждаются и созревают до задачи.
Недооформленная идея, поданная сразу как Issue, не ускоряется — она просто
кладёт вопрос без ответа в очередь, которая рассчитана на задачи с ответом.

Поэтому создание Issues доступно только участникам проекта. Это не забор вокруг
проекта: Discussions открыты для всех и являются главным входом.

---

## What should a smartwatch actually do?

Most smartwatch software is built by copying features from phones and other
watches. There is a great deal of it, and much of it exists because it could be
made rather than because anybody wanted it.

Attadipa is open. Tell us what would genuinely be useful on your wrist: something
you can understand or act on in seconds, something that still matters when your
phone has no signal, or simply something nobody has built properly yet.

Ideas that make the most of being on a wrist rather than in a pocket are the
most interesting ones — quick, contextual, useful in a real situation, and
often stronger for working offline, over mesh, with navigation, local
connectivity, sensors or an Attadipa node. That list is a hint, not a boundary. If
your idea needs none of it, it is still welcome.

**[Post an app idea →](https://github.com/hleserg/Attadipa/discussions/new?category=ideas)**

---

## Что на самом деле должны уметь умные часы?

Большинство приложений для умных часов сделано переносом функций с телефонов и
с других часов. Их очень много, и значительная часть существует потому, что их
было можно сделать, а не потому, что они кому-то понадобились.

Attadipa открыт. Расскажите, что было бы по-настоящему полезно именно у вас на
запястье: то, что понимаешь или делаешь за считаные секунды; то, что не теряет
смысла, когда у телефона нет сети; или просто то, что до сих пор никто не сделал
как следует.

Интереснее всего идеи, которые выигрывают именно от того, что устройство на
запястье, а не в кармане: быстрые, контекстные, полезные в конкретной ситуации —
и часто более сильные оттого, что работают офлайн, через меш, с навигацией, с
локальной связью, с датчиками или с Attadipa node. Этот список — подсказка, а не
граница. Если вашей идее ничего из перечисленного не нужно, она всё равно нужна
нам.

**[Предложить идею приложения →](https://github.com/hleserg/Attadipa/discussions/new?category=ideas)**

---

## If you want to write code

Read [AGENTS.md](AGENTS.md) first — it is the working agreement for this
repository and it applies to people as well as to agents. The parts that matter
most before a first pull request:

- **Never trust, verify.** No code may depend on a hardware fact that has not
  been traced to a datasheet, a schematic for the specific board revision, or
  vendor source. This project has already been bitten: the T-Watch ships with
  one of five radio chips and one of two GNSS modules, and two of those five are
  not LoRa transceivers at all.
- **Mock is not hardware.** Never write `PASS` for a test that did not run on a
  physical board. Write `NOT EXECUTED — HARDWARE REQUIRED`, and label numbers
  `MEASURED`, `ESTIMATED` or `UNKNOWN`.
- **Reuse before writing.** Check
  [`docs/research/REUSE_LEDGER.md`](docs/research/REUSE_LEDGER.md) before
  implementing anything non-trivial, and check the licence before depending on
  anything.
- **Applications ask what the device can do, never which device it is.**
  `#ifdef BOARD_X` must not appear in `core/` or `apps/`.

[GitHub Issues](https://github.com/hleserg/Attadipa/issues) are the task queue;
linked pull requests and their checks show current implementation status.

## Licensing contributions

By submitting a contribution for inclusion in Attadipa, you agree that it may
be distributed under the same `GPL-3.0-or-later` license as the project.
Contributors retain copyright and authorship in their work. Submit only work you
have the right to contribute and that is compatible with the project's license.
No separate Contributor License Agreement is required.

## Лицензирование вклада

Отправляя вклад для включения в Attadipa, вы соглашаетесь с тем, что он может
распространяться по той же лицензии `GPL-3.0-or-later`, что и проект. Автор
сохраняет авторские права и авторство на свой вклад. Отправляйте только
материалы, которые вы вправе передать проекту и лицензия которых совместима с
лицензией проекта. Отдельное Contributor License Agreement не требуется.

## How the agent queue works

Work reaches coding agents as GitHub Issues carrying a machine-readable marker,
described in
[`docs/automation/AI_TASK_PROTOCOL.md`](docs/automation/AI_TASK_PROTOCOL.md).
A Discussion is **not** a task for an agent, and nothing converts one into an
issue automatically. The step from one to the other is a maintainer deciding
that an idea is actionable — which is the point of having two places rather
than one:

```
Discussion
    ↓  a maintainer decides it is actionable
Issue
    ↓
agent
    ↓
pull request
```
