**Title:** Group radar — who's still nearby? / Радар группы — кто ещё рядом?
**Category:** Ideas
**Label:** `community: app idea`

---

### The problem / Проблема

A group is moving and slowly coming apart, and nobody notices until it has
already happened. Somebody took a different fork twenty minutes ago. Somebody
stopped to adjust a boot and is now well behind. Somebody left the festival
entirely and went to a different stage. The group finds out at the next planned
stop, which is far too late to do anything cheap about it.

The question is not "where exactly is each person" — that is the previous idea.
It is coarser and asked more often: **are we still together, and who have we
lost?**

**Проблема.** Группа движется и потихоньку рассыпается, и никто этого не
замечает, пока это уже не произошло. Кто-то двадцать минут назад свернул не туда.
Кто-то остановился поправить ботинок и сильно отстал. Кто-то вообще ушёл с
фестиваля на другую сцену. Группа узнаёт об этом на следующей плановой остановке —
слишком поздно, чтобы исправить дёшево.

Вопрос здесь не «где именно каждый» — это предыдущая идея. Он грубее и задаётся
чаще: **мы ещё вместе и кого мы потеряли?**

### Why on the wrist? / Почему на запястье?

Because it is an ambient question, not a task. You want it answered by looking
down for a second while walking, several times an hour, with no intention behind
it. Anything that requires opening something will not be done often enough to
catch the problem while it is still small — and the whole value here is catching
it early, when the answer is "shout" rather than "go back three kilometres".

**Почему на запястье.** Потому что это фоновый вопрос, а не задача. Ответ нужен
по взгляду вниз на секунду, на ходу, несколько раз в час, без всякого намерения.
Всё, что требует что-то открыть, не будет делаться достаточно часто, чтобы
поймать проблему маленькой, — а вся ценность именно в раннем обнаружении, когда
решение звучит как «крикнуть», а не «вернуться на три километра».

### Scenario / Сценарий

Festival · hiking · skiing · family groups · a large park · quests and outdoor
games · children · any outdoor event where a group is spread out and moving.

**Сценарий.** Фестиваль · поход · горные лыжи · семейные группы · большой парк ·
квесты и игры на местности · дети · любое мероприятие, где группа растянута и
движется.

### Five-second interaction / Взаимодействие за несколько секунд

A single glanceable list or ring of the group, where each person carries a state
that can be read without thinking:

- heard over mesh recently;
- position known and recent;
- nearby now;
- position known but stale;
- not reachable at all.

Rough distance where a position exists. Nothing to tap in the normal case — the
screen is the answer.

**The crucial part is that those states stay separate.** The temptation is to
collapse them into a green dot and a red dot, and that would destroy the whole
point: "I can hear their radio but have no idea where they are" and "I know
exactly where they were forty minutes ago and cannot hear them now" are
completely different situations that call for completely different reactions.

**Взаимодействие за секунды.** Один список или кольцо группы, где у каждого
состояние, читаемое без раздумий:

- недавно слышен по мешу;
- координаты известны и свежие;
- сейчас рядом;
- координаты известны, но устарели;
- недостижим вообще.

Примерная дистанция там, где координаты есть. В обычном случае нажимать нечего —
экран и есть ответ.

**Главное — чтобы эти состояния не слились.** Соблазн свернуть их в зелёную и
красную точку велик, и это убило бы всю идею: «его рацию слышно, но где он —
неизвестно» и «я точно знаю, где он был сорок минут назад, и сейчас его не
слышно» — совершенно разные ситуации, требующие совершенно разных действий.

### Offline

Fully offline. It is a picture of the local mesh, which is precisely what is
still there when nothing else is.

**Офлайн.** Полностью. Это картина локального меша — именно того, что остаётся,
когда не остаётся ничего другого.

### Useful capabilities / Возможности

Mesh · GNSS / position · haptics · Attadipa Node.

**Возможности.** Меш · координаты · вибрация · Attadipa Node.

### Existing alternatives / Что уже существует?

Phone-based friend maps — Find My, Zenly while it existed, Snap Map — are the
closest in spirit and are the wrong shape twice over: they need a network, and
they are a map you open rather than a state you glance at.

Radio users approximate this by roll call: "everyone check in". It works and it
is socially expensive, so it happens rarely, which means it catches problems
late.

Mesh messengers such as Meshtastic already show nodes and last-heard times, and
this idea is close to their node list — but presented as a phone screen with
technical vocabulary, aimed at somebody interested in the mesh. This would be
aimed at somebody interested in their friends, on a device they are already
wearing.

Nothing found so far keeps radio-reachability and position-freshness visibly
distinct, which is the part that seems most worth trying.

**Что уже существует.** Карты друзей на телефоне — Find My, Zenly пока
существовал, Snap Map — ближе всего по духу и не подходят дважды: им нужна сеть,
и это карта, которую открывают, а не состояние, на которое смотрят.

Пользователи раций приближают это перекличкой: «все на связь». Работает, но
социально дорого, поэтому делается редко — и проблемы ловятся поздно.

Меш-мессенджеры вроде Meshtastic уже показывают узлы и время последнего контакта,
и идея близка к их списку узлов — но подана как экран телефона с технической
лексикой, для человека, которому интересен меш. Здесь — для человека, которому
интересны его друзья, на устройстве, которое уже надето.

Ничего, что держало бы достижимость по радио и свежесть координат наглядно
раздельно, пока не нашлось — и это, кажется, самая интересная часть.

### Anything else / Дополнительно

This is not a social network and not a friend map. There is no feed, no history
and no presence for its own sake. It answers "are we still together?" and
otherwise stays out of the way.

The honesty rules from the other ideas apply here too, and more sharply, because
this screen is a summary and summaries are where nuance goes to die:

- radio reachability and position freshness must never be merged into one
  indicator;
- distance should only appear where a position actually exists, and should carry
  its age;
- a person whose watch has no GNSS of its own may still be perfectly reachable
  over mesh through an Attadipa node. "I can hear you" and "I know where you are"
  are separate facts and should look separate.

The intended interaction is a glance that either reassures or names exactly who
to worry about — and says "I don't know" where it does not know.

**Дополнительно.** Это не социальная сеть и не карта друзей. Ни ленты, ни
истории, ни присутствия ради присутствия. Экран отвечает на «мы ещё вместе?» и в
остальное время не мешает.

Правила честности из других идей действуют и здесь, даже жёстче, потому что это
сводный экран, а именно в сводках нюансы и погибают:

- достижимость по радио и свежесть координат нельзя сливать в один индикатор;
- дистанция должна появляться только там, где координаты действительно есть, и
  нести свой возраст;
- человек, у которого в часах нет собственного GNSS, может при этом прекрасно
  быть на связи через меш и Attadipa node. «Я тебя слышу» и «я знаю, где ты» —
  разные факты, и выглядеть они должны по-разному.

Задумка — взгляд, который либо успокаивает, либо называет, за кого именно стоит
волноваться, и говорит «не знаю» там, где не знает.
