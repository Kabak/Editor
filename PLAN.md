# План модификации Editor (Qt5/Qt6, C++17)

Репозиторий: https://github.com/Kabak/Editor · изучен полностью (3829 строк исходников).
Приоритет: **скорость работы редактора** и качество (каждая фаза собирается и проходит тесты).

---

## 0. Что уже есть в коде (аудит)

| Файл | Роль | Что важно для наших изменений |
|---|---|---|
| `src/editor.*` | `Editor : QPlainTextEdit` | уже вызывает `setViewportMargins(0,0,OverviewRuler::Width,0)`; `viewportEvent` переставляет ruler → **левое поле под нумерацию встраивается сюда же** |
| `src/highlighter.*` | один `QSyntaxHighlighter`, слои: tree-sitter spans → Blocks → Phrases → Find | `highlightBlock` вызывается на блок; `refreshBatch()` режет работу по **8 мс**; `parse()` копирует **весь документ** (`toPlainText()`) — главный тормоз |
| `src/rules.*` | Aho-Corasick (`PhraseMatcher`) + префиксный бор (`BlockMatcher`) | готовый быстрый движок для ключевых слов; сейчас **всегда** case-fold → нужен флаг регистра |
| `src/searchindex.*`, `matchindex.h` | поиск Find короткими шагами по **4 мс**, лимит 200 000 совпадений | образец «не блокировать UI»; переиспользуем для синтаксиса |
| `src/overviewruler.*` | полоса меток справа, кэш строк по generation | не трогаем, только не сломать margins |
| `src/window.*` | меню, тулбар, табы, `splitter(tabs | searchPanel)` | `styleToolbarButtons()` шьёт QSS **в каждый баттон**; `Coloring Settings` — отдельное меню |
| `src/dialogs.*` | `ThemeColorsDialog` — один `QFormLayout`, Day и Night вперемешку | переверстаем в две колонки |
| `src/settings.*` | `settings.ini` + `op_doc.ini` + `Profiles/*.prf`, атомарная запись, бэкап битых | добавляем новые ключи и stores по тому же образцу |
| `src/theme.cpp` | только `QPalette`, QSS нет | сюда встраивается QSS-слой |
| `CMakeLists.txt` | FetchContent: tree-sitter + cpp + json грамматики (3 загрузки) | удаляются → сборка быстрее и **офлайн** |

**Важные факты, влияющие на план:**
1. `README.md` прямо говорит: *«The current feature update is source-only. It has not been compiled or run.»* → **нулевая фаза — собрать и прогнать тесты**, иначе будем строить на непроверенном фундаменте.
2. `ROADMAP.md` уже содержит пункт «Review the built-in syntax highlighting (probably remove it)» — наш п.3 совпадает с планом автора.
3. Тесты есть: `editor_tests` (508 строк, offscreen), `encoding_tests`, `ruler_tests`. Хорошая база, чтобы не сломать существующее.
4. Приоритет сейчас: **syntax < Blocks < Phrases < Find** (`highlightBlock` красит в этом порядке).

---

## 1. Ответ на п.9 (QSS и Outline) — коротко

**Да, QSS — правильный инструмент, и он покрывает всё, что ты хочешь.**

- CSS-свойства `outline` в Qt QSS **нет**. Его роль играет `border`: `border: 2px solid #7aa7e8; border-radius: 4px;` — визуально это и есть outline.
- Состояния поддерживаются: `:hover`, `:pressed`, `:checked`, `:focus`, `:disabled`, а также селекторы по `objectName`: `QToolButton#findEnabled { … }`.
- Что нельзя без QSS: рамку/скругление/отступы кнопки. `QPalette` управляет только цветами фона/текста, геометрию рамки не задаёт.
- Альтернативы без QSS (дороже и хуже поддерживаются): `QProxyStyle` с переопределением `drawPrimitive(PE_PanelButtonCommand / PE_FrameFocusRect)` или ручной `paintEvent`. Рекомендую QSS, а `QProxyStyle` оставить как точечный костыль, если понадобится что-то экзотическое.
- Подводные камни, которые учтём: QSS на `QToolButton`/`QPushButton` **отключает нативную отрисовку** → нужно явно задать `padding`, `min-height`, иначе кнопки «сплющатся»; и нельзя одновременно держать per-widget `setStyleSheet` (как сейчас в `styleToolbarButtons`) и глобальный QSS — они конфликтуют. Поэтому per-widget стили убираем.
- Производительность: QSS парсится один раз при смене темы/загрузке файла, **не** на каждое нажатие клавиши. На скорость редактирования не влияет.

Схема: встроенные `themes/day.qss` + `themes/night.qss` (в ресурсах) → копируются в `<config>/Themes/` при первом запуске → пользователь правит текстом → цвета подставляются из `ThemeColors` через плейсхолдеры (`%buttonOff%`, `%selection%`, `%closeTab%`…), чтобы диалог настроек и QSS не расходились. Пункт меню «Reload theme» применяет правки без перезапуска.

---

## 2. Ключевые архитектурные решения

### 2.1 Новый движок синтаксиса (замена tree-sitter)

Отказываемся от tree-sitter полностью (п.3) и делаем **поблочный лексер**:

- один проход по строке блока, **без регулярных выражений** в горячем пути: распознаём комментарии, строки, числа (`0x`, `0o`/`0`, `0b`, десятичные, float, суффиксы `u/l/f/ull`), идентификаторы;
- категория идентификатора — lookup в `QHash` (keywords / reserved / types / builtins), собранный из `.lsh`;
- ключевые слова дополнительно могут идти через существующий `PhraseMatcher` **с проверкой границ слова** (новый флаг `wholeWord`);
- имя функции = идентификатор, за которым следует `(`; имя класса = идентификатор перед `::` или после `class/struct/enum`;
- **состояние между строками** (многострочный комментарий, raw-string, heredoc, continued string) хранится в состоянии блока Qt. Сейчас состояние занято правилами Blocks (`SeparatorState/UncoloredState/FirstRuleState+n`) → **упаковываем оба состояния в один int**: младшие 16 бит — Blocks, старшие — синтаксис. Это критичная деталь, иначе многострочные комментарии будут «терять» цвет;
- никаких `toPlainText()` на весь документ: `grep` по исходникам станет частью приёмки;
- лимиты: на очень длинную строку (> 64 КБ) — раскраска первых N символов + флаг «дальше не раскрашено», чтобы одна строка на 10 МБ не подвешивала UI.

**Выигрыш:** уходит парсинг всего файла, 3 сетевые загрузки при сборке, ~1.5 МБ кода грамматик в бинаре, зависимость и лицензии tree-sitter. Сборка становится офлайн-возможной.

### 2.2 Формат `.lsh` (папка `LSHighlighter/`)

Читаемый INI, как `.prf`. Пример `LSHighlighter/Cpp.lsh`:

```ini
[language]
name=C++
extensions=cpp,hpp,cc,hh,cxx,hxx,c,h
detect=#include|template\s*<|std::
caseSensitive=true
enabled=true

[comments]
line=//
blockStart=/*
blockEnd=*/
color=#6a9955
enabled=true

[strings]
delimiters=",'
escape=\
color=#ce9178
enabled=true

[preprocessor]
prefix=#
color=#c586c0
enabled=true

[keywords]
color=#569cd6
words=alignas,auto,bool,break,case,catch,class,const,constexpr,continue,decltype,default,delete,do,else,enum,explicit,export,extern,false,for,friend,goto,if,inline,mutable,namespace,new,noexcept,nullptr,operator,private,protected,public,register,return,sizeof,static,static_assert,struct,switch,template,this,throw,true,try,typedef,typeid,typename,union,using,virtual,void,volatile,while

[reserved]
color=#4ec9b0
words=override,final,const_cast,dynamic_cast,reinterpret_cast,static_cast

[types]
color=#4ec9b0
words=int,char,short,long,float,double,unsigned,signed,size_t,int8_t,int16_t,int32_t,int64_t,uint8_t,uint16_t,uint32_t,uint64_t,wchar_t,char16_t,char32_t,string,vector,map

[numbers]
enabled=true
decimal=#b5cea8
hex=#b5cea8
octal=#b5cea8
binary=#b5cea8
float=#b5cea8

[functions]
color=#dcdcaa
enabled=true

[classes]
color=#4ec9b0
enabled=true

[operators]
color=#d4d4d4
enabled=true
```

- секция `enabled=false` в любом блоке = выключение одной категории; `enabled=false` в `[language]` = выключение языка целиком (требование п.3);
- «x32/x64 переменные» покрываются `[types]` (`int32_t`, `uint64_t`, `DWORD`, `QWORD`, `size_t`…) — при желании выделю отдельную секцию `[types_width]` со своим цветом;
- хранение: встроенные файлы в репо `LSHighlighter/`, рабочие копии в `<config>/LSHighlighter/` (рядом с `settings.ini`, `op_doc.ini`, `Profiles/`);
- загрузка через новый `SyntaxStore` по образцу `ProfileStore` (валидация, предупреждения, атомарная запись).

### 2.3 Определение языка

1. по расширению (из `extensions` всех `.lsh`);
2. по имени файла без расширения: `Makefile`, `Dockerfile`, `CMakeLists.txt`;
3. **по содержимому** — для вставки текста в пустой документ (требование п.3): shebang `#!/usr/bin/env python`, `<?php`, `<!DOCTYPE html`, `{`+`"key":`, `#include`, `section .text`, `ORG 0x` и т.п.

Эвристики читают **только первые ~64 КБ**, запускаются при загрузке/первой вставке и с debounce ~150 мс, **никогда на каждое нажатие клавиши**. Результат — имя языка в статус-баре (заменяет нынешние «C/C++», «JSON», «Plain text»).

### 2.4 Нумерация строк (п.1, п.2)

- `LineNumberArea` — дочерний виджет `Editor`, рисуем в `paintEvent` **только видимые блоки**;
- `setViewportMargins(left, 0, OverviewRuler::Width, 0)` — сосуществует с overview ruler;
- ширина = `horizontalAdvance('9') * цифры(blockCount) + padding`, пересчёт только при изменении числа блоков (кэш, не на каждый paint);
- при WordWrap номер рисуется **у первой экранной строки** логического блока (как VS Code), через `blockBoundingGeometry()` + `contentOffset()`;
- обновление по `updateRequest(QRect, int dy)` со скроллом `area->scroll(0, dy)` — без полного repaint;
- кнопка `Str. Numbers` — checkable `QAction` в тулбаре **справа от Find Color**, `objectName=lineNumbers`, входит в общий стиль кнопок;
- состояние **на каждый таб**: `Editor::lineNumbersMode {Auto, On, Off}`; `Auto` = включено, если расширение в расширяемом списке `settings.ini → [lineNumbers] autoExtensions=…` (или язык определён лексером);
- список авто-включений редактируется в UI (Settings → Line Numbers…), сохраняется в ini;
- предложение: перекрывающее состояние хранить в `op_doc.ini` per-file, чтобы при следующем запуске файл открывался с тем же выбором.

### 2.5 Плавающее окно Find (п.7)

- новый `FindWindow` (`Qt::Tool`, родитель — главное окно) с содержимым нынешнего `SearchPanel` + кнопка `Aa` (case sensitive) + кнопка закрытия;
- `Ctrl+F` — показать/поднять и сфокусировать (с выделением в поле), `Esc` — скрыть, `F3`/`Shift+F3` работают и когда фокус в редакторе;
- позиция и размер сохраняются в `settings.ini → findWindow/geometry`, флаг регистра — `findWindow/caseSensitive` (требование 7.1);
- `splitter` убирается, центральный виджет — табы; ключ `window/splitter` читаем для миграции и больше не пишем;
- **регистр в поиске**: `PhraseMatcher` получает флаг `foldCase`; один и тот же автомат используют `SearchIndex` (счётчик + ruler), `highlighter` (подсветка) и навигация (`FindCaseSensitively`) → счётчик, подсветка и F3 всегда согласованы. Обязательный тест на это.

### 2.6 Тема и настройки (п.4, 5, 6)

- `ThemeColors` расширяется: `selection` (уже есть — переименовать в UI в «Selected Text Color»), **`closeTab`** (фон кнопки закрытия таба) + опционально `closeTabHover`;
- `findColor` переезжает в `ThemeColorsDialog`, меню `Coloring Settings` удаляется, три чекбокса переезжают в `Settings`/тулбар (в тулбаре они уже есть);
- новая вёрстка: `QGridLayout`, слева все Day, справа все Night, одна настройка напротив другой; глобальные (Find Color, если решим оставить глобальным) — в отдельной группе с разделителем; подсказка про toolbar buttons — под сеткой;
- порядок строк: Editor background · Menus and window · Selected text · Close-tab button · Toolbar buttons default · Toolbar buttons pressed;
- фон кнопки закрытия таба красится через QSS: `QTabBar::close-button { background: %closeTab%; }` (+`:hover`).

### 2.7 Шрифт по умолчанию (п.8)

При первом запуске (нет `settings.ini`) выбираем первый доступный: **Consolas → Cascadia Mono → DejaVu Sans Mono → Liberation Mono → Menlo → Courier New → systemFont(FixedFont)**, regular, 11 pt, и сразу фиксируем в ini. Consolas есть только в Windows — на Linux сработает fallback, это ожидаемо.

---

## 3. Фазы (порядок оптимальный по риску и по влиянию на скорость)

Каждая фаза = компилируется + зелёные тесты + запись в `ROADMAP.md`. Никаких «source-only» изменений.

### Фаза 0 — фундамент (обязательная)
- поставить тулчейн (Qt5 + cmake + ninja), собрать `text_editor` и **все три теста**, зафиксировать baseline;
- починить то, что не собирается (README предупреждает, что эта версия не компилировалась);
- добавить бенчмарк-заготовку: файл 5 МБ, замер latency нажатия клавиши и времени полной раскраски;
- (предложение) GitHub Actions: сборка + тесты на Ubuntu, чтобы «не собрано» больше не повторялось.

**Приёмка:** `ctest` зелёный, есть числа baseline по производительности.

### Фаза 1 — нумерация строк + `Str. Numbers` (п.1, 2)
Изолированно, быстро, сразу видно результат. Файлы: `editor.*`, новый `linenumberarea.*`, `window.*` (кнопка, меню, per-tab), `settings.*` (autoExtensions), тесты.
**Приёмка:** номера не «плывут» при скролле и wrap, ruler не сдвинут, zoom Ctrl+колесо меняет ширину колонки, кнопка работает per-tab и в DAY/NIGHT выглядит как соседние.

### Фаза 2 — ядро нового синтаксиса + удаление tree-sitter (п.3, ядро)
Самая важная для скорости фаза. Новый `syntax.*` (лексер + `SyntaxStore` + `.lsh`), упаковка состояний блока, слой синтаксиса в `highlighter`, удаление `editor_parser`, FetchContent, `detectLanguage`, лицензий tree-sitter из install.
Сначала **паритет по C/C++ и JSON** на реальных файлах (сравнение скриншотов со старой сборкой), потом удаляем старый путь.
**Приёмка:** нет `toPlainText()` в горячем пути; latency на 5 МБ не хуже baseline (ожидаемо лучше); многострочные комментарии/строки красятся корректно через границы блоков; сборка проходит **без сети**.

### Фаза 3 — языковой пакет и детекция (п.3, данные)
`.lsh` для всех языков списка: C, C++, C#, Python, JSON, PHP, JS, TypeScript, Java, pic18/16 ASM, x86 ASM, 8051, Z80, AVR, MSP430, Rust, Go, Pascal, CSS, HTML, SQL, Bash, PowerShell, Lua, R, Ruby, Kotlin, Perl, Fortran, COBOL, Dockerfile, XML, CMake, Makefile, Regex, YAML.
Плюс UI `Settings → Languages Syntax Highlighter` (список языков, редактор категорий/цветов, включение/выключение категории и языка, Save/Close с запросом сохранения — как в профилях), детекция по содержимому и вставке.
**Приёмка:** каждый язык открывает свой тестовый файл с ожидаемой раскраской; выключение категории/языка работает мгновенно; вставка кода в пустой таб определяет язык.
*Данные можно делать волнами: сначала C/C++/C#/Python/JSON/PHP/JS/Java + ASM-семейство, потом остальное — это не блокирует другие фазы.*

### Фаза 4 — тема и настройки (п.4, 5, 6)
`ThemeColors` + `closeTab`, перенос Find Color, удаление `Coloring Settings`, двухколоночный `ThemeColorsDialog`, миграция ключей ini.
**Приёмка:** все прежние настройки читаются из старого ini без потерь; Day/Night напротив друг друга; фон выделения и кнопки закрытия таба меняются сразу.

### Фаза 5 — QSS-слой и шрифт (п.8, 9)
`theme.cpp` → палитра + QSS с плейсхолдерами, файлы `Themes/*.qss` в конфиге, «Reload theme», удаление per-widget `setStyleSheet`, стиль `Str. Numbers` через тот же QSS, дефолтный Consolas/monospace.
**Приёмка:** outline кнопок настраивается текстом в QSS и переживает DAY/NIGHT; кнопки не «сплющены»; смена темы < 50 мс; шрифт по умолчанию monospace на обеих ОС.

### Фаза 6 — плавающий Find (п.7)
`FindWindow`, удаление сплиттера, горячие клавиши, сохранение позиции, кнопка `Aa` + case-sensitive во всех трёх местах поиска.
**Приёмка:** окно открывается там, где закрыли; регистр сохраняется после перезапуска; счётчик = подсветка = F3 при обоих режимах регистра; на 5 МБ файле ввод в поле не подтормаживает (шаги по 4 мс сохранены).

### Фаза 7 — доводка
Тесты на всё новое, `docs/manual-tests.md` дополнить, обновить `README.md`/`ROADMAP.md`/`agents.md`, проверить `Dockerfile.windows` (MXE/Qt5 static) и `Dockerfile.linux-appimage` — после удаления tree-sitter они становятся проще и собираются офлайн.

---

## 4. Производительность: конкретные бюджеты и приёмы

| Метрика | Бюджет | Как обеспечиваем |
|---|---|---|
| Отклик на нажатие клавиши (файл 5 МБ) | < 16 мс | поблочный лексер, никаких проходов по всему документу |
| Порция перекраски | 8 мс (как сейчас) | `refreshBatch()` сохраняем |
| Шаг поиска Find | 4 мс (как сейчас) | `SearchIndex` не меняем по механике |
| Определение языка | ≤ 64 КБ, debounce 150 мс | только при загрузке/первой вставке |
| Нумерация строк | только видимые блоки | кэш ширины, скролл вместо repaint |
| Смена темы / QSS | < 50 мс, один раз | парсинг QSS вне горячего пути |
| Память | нет копии документа на таб | убираем `toPlainText()` из синтаксиса |

Дополнительно: очень длинные строки ограничиваем; automaton строим один раз на изменение запроса/правил, а не на каждый блок; все новые файлы — без `QRegularExpression` в `highlightBlock`.

---

## 5. Тесты, которые добавим

- `.lsh`: парсинг, битые значения → дефолт + предупреждение, round-trip save/load;
- лексер: многострочные комментарии/строки через границы блоков, числа `0x/0b/0o/float`, границы слов (keyword не должен краситься внутри идентификатора);
- приоритет слоёв: Syntax < Blocks < Phrases < Find (расширение существующего теста);
- нумерация: ширина при 9→10→100 строках, paint при wrap, сосуществование с ruler;
- поиск: case-sensitive vs insensitive — счётчик, подсветка и навигация совпадают;
- миграция настроек: старый `settings.ini` (со `splitter`, `highlight/findColor`) читается корректно;
- тема: новые поля применяются к палитре и QSS;
- бенчмарк 5 МБ как отдельный (не обязательный) тест.

---

## 6. Риски

| Риск | Митигация |
|---|---|
| Состояние блока Qt уже занято правилами Blocks | упаковка в один int + тест на многострочные комментарии; это делаем в Ф2 первым делом |
| Удаление tree-sitter ухудшит раскраску C++ (строки/комментарии) | лексер покрывает строки/комментарии лучше, чем substring-профили; сравнение до/после на реальных файлах |
| QSS ломает нативный вид кнопок на Win7/Qt5.15 | явные padding/min-height, проверка в MXE-сборке |
| Плавающий Find на X11/Wayland ведёт себя по-разному | `Qt::Tool` с родителем, fallback на frameless + свой drag; ручной чек-лист |
| 35+ языков = большой объём данных | волны, данные отделены от кода, ошибки в `.lsh` не роняют редактор |
| Проект не компилируется «как есть» | Фаза 0 обязательна |

---

## 7. Вопросы (нужны короткие ответы)

1. **Приоритет синтаксиса:** ставлю новый слой Syntax **ниже** Blocks → Phrases → Find (как сейчас с tree-sitter). Подтверждаешь? Или синтаксис должен быть выше Blocks?
2. **Find Color:** оставить **одним глобальным** значением (в группе «глобальные» в Theme Colors) или сделать **раздельно Day/Night**?
3. **Нумерация:** нужна ли подсветка номера текущей строки и/или относительные номера (как vim)? Хранить per-file выбор в `op_doc.ini`?
4. **Find-окно:** системная рамка (`Qt::Tool`, надёжнее на Win7/X11) или **frameless** со своим перетаскиванием за тело (красивее, но больше кода)? `Esc` закрывает — ок?
5. **Языки:** делаем волнами (сначала C/C++/C#/Python/JSON/PHP/JS/Java + ASM) или все 35 сразу? Для **pic18/16 ASM** есть предпочтительный источник мнемоник (MPASM / PIC18 instruction set), или брать стандартный список Microchip?
6. **Qt:** основная цель остаётся Qt 5.15 (Windows 7) + Qt6 как опция? Или Qt6 уже основной?
7. **Статус-бар:** после удаления tree-sitter показывать имя языка из `.lsh` (например «C++», «Python»), а не имя цветового профиля — верно?

## 8. Предложения сверх задания (дёшево и полезно)

- **Whole-word** опция для phrase-правил в `.prf` (уже намечено в ROADMAP) — профили станут точнее.
- Подсветка текущей строки + indent guides — почти бесплатно поверх готовой нумерации.
- Подсветка парных скобок — бесплатно поверх лексера.
- «Reload theme» и «Reload language definitions» в меню — правки QSS/`.lsh` без перезапуска.
- GitHub Actions: сборка + `ctest` на каждый push.
- Бенчмарк-скрипт `tools/bench.sh` (файл 5 МБ, замер latency), чтобы «быстро» было числом, а не ощущением.