**Title:** Offline friend location — find your group without cell service / Координаты друзей офлайн — найти свою группу без мобильной связи
**Category:** Ideas
**Label:** `community: app idea`

---

### The problem / Проблема

A group splits up somewhere with no mobile coverage — a forest, a valley, a
festival field with 40,000 people and a saturated cell tower — and there is no
way to answer the simplest question anybody has: *where is everybody?* You end
up standing on a rise holding a phone above your head, or agreeing on a meeting
time hours in advance and hoping.

The failure is not dramatic. It is twenty minutes lost, twice a day, and a
low-level worry that never quite goes away when children are in the group.

**Проблема.** Группа разделилась там, где нет сотовой связи — лес, ущелье,
фестивальное поле с сорока тысячами человек и перегруженной вышкой, — и ответить
на самый простой вопрос невозможно: *где все?* В итоге стоишь на пригорке с
телефоном над головой или договариваешься о встрече на несколько часов вперёд и
надеешься.

Это не катастрофа. Это двадцать потерянных минут дважды в день и фоновая
тревога, которая не отпускает, когда с вами дети.

### Why on the wrist? / Почему на запястье?

Because the question is asked constantly and answered in a glance. Ten times an
hour, "are they still behind me?" — and each time, taking out a phone, waking
it, finding the app and waiting for a map to draw costs more than the answer is
worth, so you stop asking and just worry instead.

Hands are also frequently unavailable: trekking poles, a child, a bike, gloves,
rain. A wrist survives all of those. A phone does not.

**Почему на запястье.** Потому что вопрос задаётся постоянно, а ответ нужен
одним взглядом. Десять раз в час: «они ещё позади?» — и каждый раз достать
телефон, разбудить, найти приложение и дождаться отрисовки карты стоит дороже,
чем сам ответ. В итоге перестаёшь спрашивать и просто тревожишься.

Плюс руки часто заняты: палки, ребёнок, велосипед, перчатки, дождь. Запястье это
переживает, телефон — нет.

### Scenario / Сценарий

Hiking · forest · festival · camp · large outdoor events · family trips ·
children · skiing.

The common shape: several people who *chose* to be together, temporarily spread
over a few hundred metres to a few kilometres, with no usable cell service.

**Сценарий.** Поход · лес · фестиваль · лагерь · крупные мероприятия на открытом
воздухе · семейные поездки · дети · горные лыжи. Общая форма одна: несколько
человек, которые *хотят* быть вместе, временно растянулись на сотни метров или
километры, и сотовой связи нет.

### Five-second interaction / Взаимодействие за несколько секунд

Raise the wrist and read, without touching anything:

- **who** is selected,
- **how far** away they are,
- **which way**, if a direction can honestly be produced,
- **how old** that information is.

One rotation or swipe moves to the next person in the group. That is the whole
interaction. No map, no pinch-zoom, no loading spinner.

The age of the position is not a detail to be tucked away — it belongs on the
same screen as the distance, because "800 m north-east" means something entirely
different at 30 seconds old and at 40 minutes old.

**Взаимодействие за секунды.** Поднять руку и прочитать, ничего не нажимая: кто
выбран, на каком расстоянии, в какую сторону — если направление можно получить
честно, — и насколько свежие эти данные. Поворот или свайп переключает на
следующего участника. Всё. Ни карты, ни зумов, ни спиннера.

Возраст координат — не мелкий технический факт, которому место в углу: он должен
стоять рядом с расстоянием, потому что «800 м на северо-восток» значит совершенно
разное через 30 секунд и через 40 минут.

### Offline

Fully offline. This idea has no meaning otherwise — if there were coverage,
everyone would use the messenger they already have. Positions would travel
between devices over mesh, device to device, with no tower and no account.

**Офлайн.** Полностью. Иначе идея бессмысленна: была бы связь — все пользовались
бы уже установленным мессенджером. Координаты передавались бы между устройствами
по мешу, напрямую, без вышки и без аккаунта.

### Useful capabilities / Возможности

Mesh · GNSS / position · heading · haptics · Attadipa Node.

**Возможности.** Меш · координаты · направление · вибрация · Attadipa Node.

### Existing alternatives / Что уже существует?

Phone apps that share location — Find My, Google Maps sharing, Telegram live
location — all of which need a network and therefore fail in exactly the
situation that creates the problem.

Dedicated devices do exist: Garmin inReach and Zoleo do off-grid messaging with
position, and rally teams use radios with GPS reporting. Those work, but they are
expensive, subscription-bound, and their group awareness is buried inside a
device you take out and operate deliberately, not something you glance at.

Traditional walkie-talkies solve a different half of the problem: they carry
voice but not position, so "where are you?" gets answered with "near the big
tree", which in a forest is not an answer.

**Что уже существует.** Приложения на телефоне — Find My, шеринг в Google Maps,
живая геопозиция в Telegram — всем им нужна сеть, поэтому они отказывают ровно в
той ситуации, которая и создаёт проблему.

Отдельные устройства есть: Garmin inReach и Zoleo умеют внесетевые сообщения с
координатами, у ралли-команд есть рации с передачей GPS. Они работают, но дорогие,
завязаны на подписку, и информация о группе спрятана внутри устройства, которое
надо достать и осознанно включить, а не бросить на него взгляд.

Обычные рации решают другую половину задачи: они передают голос, но не координаты,
поэтому на «ты где?» отвечают «у большого дерева», что в лесу ответом не является.

### Anything else / Дополнительно

The honesty constraints matter more than the feature here, and getting them
wrong would make this worse than nothing:

- Position may come from the watch's own GNSS or from an attached Attadipa node,
  and the mesh capability may likewise be local or supplied by a node. The
  interface should not care, and neither should the user.
- **A heading source may simply not exist.** Neither current board has a
  magnetometer. If direction cannot be produced honestly, the app must not draw
  a confident arrow — a bearing, or nothing, is better than a pointer that
  quietly lies. An arrow that is wrong in a forest is worse than no arrow,
  because people follow it.
- Stale coordinates must look stale at a glance, not on a details screen.

The intended interaction is a glance that either answers the question or clearly
says it cannot. Both are useful. A confident wrong answer is not.

**Дополнительно.** Ограничения на честность здесь важнее самой функции, и если
их нарушить, получится хуже, чем ничего:

- координаты могут приходить от собственного GNSS часов или от подключённого
  Attadipa node, и меш точно так же может быть локальным или от node. Интерфейсу
  это должно быть безразлично, пользователю — тем более;
- **источника направления может просто не быть.** Ни на одной из плат нет
  магнитометра. Если направление нельзя получить честно, приложение не должно
  рисовать уверенную стрелку: азимут или ничего лучше, чем указатель, который
  тихо врёт. Неверная стрелка в лесу хуже отсутствующей, потому что за ней идут;
- устаревшие координаты должны выглядеть устаревшими сразу, а не на экране
  подробностей.

Задуманное взаимодействие — взгляд, который либо отвечает на вопрос, либо ясно
говорит, что ответить не может. Полезно и то и другое. Уверенный неправильный
ответ — нет.
