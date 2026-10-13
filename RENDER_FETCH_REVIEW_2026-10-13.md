# Ревью рендера и потоковой загрузки

Дата: 13 октября 2026. База: `a8883376843b` и текущие незакоммиченные изменения. Проверены `StreamText`, собственный `streamFetch`, TSB1, iOS и Android renderer, Fabric, prepared layouts, accessibility, тесты и CI. Актуальный fetcher проекта — собственный `streamFetch`; старый nitro-fetch исключён из области этого отчёта.

## Итог исправлений — 13 октября 2026

Все восемь находок R1/R2/R3/R5/R6/R7/R8/R9 исправлены. Семь исходных дефектов были воспроизведены на выделенных iOS/Android устройствах; для R7 теперь также получен TSan warning на контрольном варианте без блокировки и чистый прогон исправленного production `DrawToken`. R4 не считается подтверждённым багом: реальный RN 0.86 не повторяет эффект в проверенном сценарии. Его тест сохранён и проходит на обеих платформах.

Сырые доказательства первичного ревью сохранены. Актуальные результаты: [validation.json](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/validation.json), [SHA-256 исходников](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/source-manifest.json).

В коммит этого ревью включены исправления R1/R2/R3/R5/R6/R7/R8/R9, их проверки, CI и удаление nitro-fetch. Более ранняя правка `StreamTextStyle.kt` вместе с `StreamTextFlowTest.kt`, оформление и телеметрия benchmark, Live/Promo demo и медиаматериалы остаются отдельно в рабочем дереве. Отчёты ниже фиксируют реально проверенное рабочее дерево: Android instrumentation 4/4 включает один более ранний Flow-тест; в этот коммит входят три R8 instrumentation-теста.

| Находка | Изменение | Доказательство после исправления |
| --- | --- | --- |
| R1: поздняя публикация подменяет source/ref/clear | В JS, Fabric state и native view согласован владелец источника; imperative attach/clear отключают named listener, устаревшие callbacks не принимаются | Четыре сценария переходов проходят на iOS и Android; native text, JS ref и события относятся к одному источнику |
| R2: удаление source не возвращает существующий named stream | Переход в named-режим разрешает текущий buffer даже при неизменном streamId | Экран и ref сразу показывают уже опубликованный named buffer на обеих платформах |
| R3: pre-aborted owned request остаётся idle | Renderer получает настоящий cancelled buffer; локальное состояние привязано к конкретным key/signal/streamId/режиму владения | Owned pre-abort даёт один cancelled terminal; shared pre-abort сохраняет существующий stream, включая переход из owned-режима |
| R5: eviction допускает повторный POST | Tombstones для методов кроме GET/HEAD сохраняются до `releaseStream` или уничтожения runtime | После 4161 настоящего named request серверный счётчик POST остаётся 1; явный release и remount повышают его до 2 |
| R6: потерянная Fabric-команда удерживает buffer | Handoff registry хранит weak_ptr; доставка забирает живой buffer, просроченные ссылки удаляются | Android: настоящий ref.attach → stopSurface → clear → Hermes GC. Registry `0 / 0 байт → 1 / 4194368 байт → 0 / 0 байт` |
| R7: data race при отмене iOS tile job | Чтение и запись cancelled синхронизированы NSLock | TSan на извлечённом production-классе проходит; тот же stress без синхронизации выдаёт `ThreadSanitizer: data race` |
| R8: supplementary letters ломают word traversal | Обратный обход использует codePointBefore и сохраняет корректные UTF-16 offsets | 7 JVM Reading-тестов и 3 Android instrumentation-теста, включая оба направления и cursor внутри surrogate pair |
| R9: новый streamId показывает предыдущее сообщение | Смена владельца/идентичности явно отсоединяет старый buffer, даже если оба prop pointers равны null | На обеих платформах новый пустой id сразу даёт пустой текст, height=0 и idle; поздние записи старого потока не возвращают текст, удаление id тоже очищает view |

Регрессионный набор: [review-repro.harness.tsx](/Users/alex/WebstormProjects/react-native-text-stream/example/__tests__/review-repro.harness.tsx), результаты [iOS 12/12](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/ios-review-final.json) и [Android 12/12](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/android-review.json), snapshots финальных прогонов — [iOS](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/ios-final-evidence.jsonl) и [Android](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/android-final-evidence.jsonl).

R6: [финальный тест](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/android-retention-final.json), [лог registry](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/android-retention-logcat.log). Первая проверка после фикса всё ещё удерживала буфер через HashMap тестового producer. Финальный тест использует production allocator через заранее отменённый `streamFetch`, освобождает реального JS-владельца и вызывает GC; дополнительные владельцы fixture исключены. До GC живой JS ref вправе удерживать buffer. Измеряется registry ownership, а не RSS; iOS stopSurface этим тестом не проверялся. Общий C++ путь проверен также native harness с ASan/UBSan, включая успешную доставку и потерянный handoff.

R7: [контроль с гонкой](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/r7-draw-token-tsan-before.log), [исправленный вариант](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/r7-draw-token-tsan-fixed.log), [воспроизводимый скрипт](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fixes/run-r7-draw-token-tsan.sh). Это изолированный macOS TSan-тест точного production `DrawToken` с заглушкой canvas, а не TSan-сборка всего UIKit-приложения. Контроль восстанавливает только несинхронизированное поле; тест ждёт завершения всех reader/writer jobs. Обычные iOS tile/renderer сценарии также проходят.

### Итоговая проверка

| Проверка | Результат |
| --- | --- |
| iOS in-app: review + renderer + fetch/seams | **156 passed, 3 platform skips**, 17 suites |
| Android in-app: review + renderer + fetch/seams + teardown | **135 passed, 27 platform skips**, 18 suites, включая одну полностью пропущенную iOS-only suite |
| Android instrumentation | **4/4**: Reading 3 + Flow 1 |
| Android JVM | **17/17**: Reading 7 + Fetch 7 + UTF-8 3 |
| Package Jest | **123/123**, 5 suites |
| Example Jest | **10/10**, 2 suites |
| Package TypeScript, example TypeScript и package build | Прошли |
| iOS Debug и Android Debug/native test APK | Собраны успешно |
| C++ и Foundation delegate с ASan/UBSan | Прошли |
| Production DrawToken под TSan | Прошёл; контроль без lock обнаруживает гонку |

Jest JSON сохраняет полный список реально исполненных тестов и пропусков. Пропуски не засчитаны как успех. Все устройства, артефакты и итоги перечислены в `validation.json`. Проверка относится к указанным Debug-сборкам и сценариям; Release, реальные телефоны, весь UIKit под TSan и длительное измерение памяти не заявляются проверенными.

G1 закрыт: CI теперь запускает fetch/seams, renderer, review и отдельный Android teardown; Android instrumentation включён явно. Логи и JSON с platform skips сохраняются как artifacts, iOS job включает постоянный DrawToken TSan-check, общие проверки — типы example. Локально выполнены эти группы тестов и проверен синтаксис конфигураций; удалённый GitHub Actions run не запускался.

Nitro-fetch полностью удалён из исполняемого проекта: workspace package, зависимости, native linking, patch, примеры и старые probes. Демо используют собственный `streamFetch`; JS-базовая линия бенчмарков использует RN XHR progress с корректным обозначением транспорта. Неиспользуемый nitro-text-decoder тоже удалён. NitroModules/Nitrogen нужны тестовому bridge и сохранены. Исторические отчёты и измерения не переписывались. Первичная проверка очистки — [fetch-cleanup/validation.json](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/fetch-cleanup/validation.json); её прежнее ограничение example typecheck устранено в текущем исправлении.

Оговорка R5: гарантия действует внутри одного runtime и до явного release, не заменяет серверную идемпотентность. Для небезопасных методов небольшие записи ключей теперь сохраняются дольше; освобождение payload по-прежнему ограничено кэшем. Этот компромисс отражён в README.

### Повторный запуск актуальных проверок

После сборки Debug-приложений, на выбранных выделенных устройствах, из корня репозитория:

```sh
rtk proxy bash scripts/test-stream-fetch-harness.sh ios all
rtk proxy bash scripts/test-stream-fetch-harness.sh android all
rtk proxy sh scripts/test-draw-token-tsan.sh
```

Для harness задать `HARNESS_APP_PATH` и `HARNESS_IOS_DEVICE`/`HARNESS_IOS_VERSION` либо `HARNESS_ANDROID_AVD`. Runner сам запускает fixture servers; порты 9877, 9878 и 19878 должны быть свободны. Платформы запускать последовательно: harness использует общий manifest. Скрипт сохраняет результаты в `HARNESS_RESULTS_DIR` или временную директорию `streamtext-harness-<platform>`. Android teardown выполняется последним в отдельной app-сессии. Android instrumentation запускается отдельно через `:react-native-text-stream:connectedDebugAndroidTest`.

## Историческая запись первичного ревью — до исправлений

Оставшаяся часть отчёта сохраняет исходные симптомы, оценки и доказательства. Указанные ниже падения, предложения и номера строк относятся к состоянию до исправлений; актуальный статус и команды приведены выше. Старые React probes включают R4 и не являются текущим CI gate.

P1 — исправить до использования затронутого пути в production; P2 — функциональный дефект или существенное нарушение контракта. «Воспроизведено» означает выполненную проверку указанного уровня, а не автоматически проверку на телефоне. «По коду» означает трассировку реализации с указанными границами доказательства.

## Результаты выполненных проверок

| Проверка | Результат и граница |
| --- | --- |
| Основной пакет Jest | 117 из 117, 5 suites, `--watchman=false` |
| TypeScript основного пакета | Обе конфигурации прошли |
| Example unit tests | 5 из 5 |
| C++ native harness с ASan и UBSan | Прошёл; registry, retention, task lifecycle, публикация данных |
| iOS fetch delegate harness с ASan и UBSan | Прошёл на Foundation/macOS; capacity и split matrix, late callbacks, cancel, startup error |
| Android JVM | Актуальные XML: Fetch 7/7, UTF-8 3/3, Reading 6/6 без failures/errors. Финальный exit code Gradle инструмент не сохранил; это результат тестовых отчётов, не подтверждение завершения всего build process |
| Новые React probes | 4 из 4 намеренно падают на ожидаемых исправных инвариантах; три группы дефектов R1, R3, R4 |
| Android word traversal | Production `StreamTextReading.kt` с заглушкой accessibility-констант: оба направления возвращают null для U+10400 |
| iOS 26.5, отдельный TextStream Review 17 Pro Max | 8 in-app сценариев: 7 ожидаемых регрессионных падений по R1×3, R2, R3, R5, R9; R4 прошёл без effect replay |
| Android API 35, отдельный textstream_review_api35 | Те же 8 in-app сценариев: 7 регрессионных падений, R4 прошёл без effect replay |
| Android instrumentation R8 | 2 исполненных теста production-класса, оба возвращают null вместо границ слова `[0,2]` |
| Android Fabric teardown R6 | Реальный ref.attach → stopSurface(1): registry остаётся `{entries:1,bytes:4194368}` вместо нуля |

Устройства созданы специально для этой проверки: iOS UDID `AF63A122-D9AE-447E-AB67-1CFB2145F2D2`, Android AVD `textstream_review_api35`, serial `emulator-5554`. Обе Debug-сборки текущих исходников успешно собраны. Проверены выбранные сценарии, а не весь renderer E2E suite или Release. Начальный Jest запуск упёрся в права Watchman; повторный запуск без Watchman выполнил все указанные JS-тесты. Android XML сохранены в [android-junit](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/android-junit).

Нативные snapshots, JS ref и события сохранены в [evidence.jsonl](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/evidence.jsonl), результаты assertions — [iOS harness](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/ios-review-harness.log) и [Android harness](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/android-review-harness.log). «Failed» здесь означает нарушение ожидаемого исправного поведения, а не успешность проверки. Первая iOS-попытка была прервана изменением тестовых файлов/Fast Refresh и не учитывается. Первая версия R5 давила на registry прямыми streamFetch без requestKey и не проверяла tombstones; доказательный запуск заменён на 4161 настоящий declarative named request.

## Рендерер и собственный streamFetch

### R1 P2 Подписка на streamId нарушает приоритет source и ref

Код: [StreamText.tsx:193](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/src/StreamText.tsx:193), [iOS streamPublished:107](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/ios/fabric/StreamTextComponentView.mm:107), [Android streamPublished:213](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/android/src/main/java/com/textstream/StreamTextView.kt:213).

У компонента остаются подписки на прежний `streamId` после `ref.attach(B)` или `ref.clear()`. JS-флаг `followsStream=false` не запрещает `watchStream` callback: `acceptStream()` снова назначает buffer, generation и following. Нативные команды также не отключают stream listener.

**Воспроизведено в React:** после attach текст `manual` меняется на `late publication`; после clear статус вместо `idle` становится `streaming`. Проверки находятся в `react-probes.tsx`, подробный вывод — [react-probes.log](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/react-probes.log).

**Подтверждено на iOS и Android:** после ref.attach native и JS переходят на `late publication`; при explicit source native показывает `named replacement`, а ref возвращает `explicit source`. После clear на iOS снова заполняется native view; на Android native остаётся пустым, но JS ref возвращает пришедший текст и `streaming`. Таким образом, платформенные последствия clear различаются, но контракт источника нарушен на обеих.

Есть более опасный вариант по коду: `<StreamText source={A} streamId="x" />`. JS намеренно читает A и не подписывается на x, но native listener при публикации B под x прикрепляет B. Экран и `getText()` относятся к разным сообщениям, а события B могут отбрасываться как чужие. На iOS и Android listener получает также стартовый wake, поэтому проблема возможна уже при наличии B в реестре.

**Исправление:** единое состояние владельца источника — explicit prop, imperative attachment или named stream — во всех трёх слоях. Смена владельца должна инвалидировать уже поставленные callback, а не только выключать синхронный lookup.

**Регрессия:** на обеих платформах проверить `id → attach → publish`, `id → clear → publish`, `source+id → publish`, `id → source → publish`. Сверять native text, ref text, height, serial и status events.

### R2 P2 Удаление source не возвращает уже существующий поток

Код: [StreamTextViewShadowNode.hpp:79](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/cpp/StreamTextViewShadowNode.hpp:79), [iOS updateProps:205](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/ios/fabric/StreamTextComponentView.mm:205).

**По коду:** после `source=A, streamId=x` удаляется только source. Props parser устанавливает source=null, но ищет buffer по x только при изменении самого streamId. Native получает очистку, listener не перезапускается, новой публикации нет. JS уже возвращается к x и может читать его существующий buffer; native остаётся пустым до следующей публикации. Это отдельная ветка переключения источников, которую нужно исправить вместе с R1.

**Подтверждено на iOS и Android:** после удаления source native text=`""`, height=0, status=`idle`, тогда как ref text=`"named buffer"`, status=`streaming`. До удаления R1 уже мог переключить native на named buffer; тест отдельно записывает состояние до и после перехода.

**Исправление:** при переходе в режим named stream немедленно присоединять текущий buffer реестра; отслеживать режим отдельно от указателя source.

**Регрессия:** publish завершённого B под x, mount с A и x, удалить A без нового fetch; экран и ref обязаны показывать B, включая после resize.

### R3 P2 Заранее отменённый declarative request остаётся в idle

Код: [useStreamRequest:367](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/src/StreamText.tsx:367), [startFetch:82](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/src/fetch.ts:82).

**Воспроизведено:** `<StreamText request={{url, signal: alreadyAbortedSignal}} />` не запускает сеть, но `getStatus()` остаётся `idle`. `startFetch()` правильно возвращает отдельный cancelled buffer без публикации в registry; hook этот buffer игнорирует и ждёт поток под своим автоматически созданным id. Terminal event отсутствует.

**Подтверждено на iOS и Android:** ref остаётся `{status:"idle",bytes:0}`, native buffer не прикреплён, terminal events отсутствуют.

Это не дефект существующей защиты shared stream от замены заранее отменённым запросом: её нужно сохранить. Проблема именно в том, что собственный запрос компонента не доводит отмену до renderer.

**Исправление:** передать отменённый buffer/состояние собственному view, не заменяя чужой именованный поток.

**Регрессия:** owned pre-abort должен давать `cancelled` и один terminal callback; shared pre-abort должен сохранять прежний поток.

### R4 Снят с подтверждённых: повтор owned POST в StrictMode

Код: [useStreamRequest:364](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/src/StreamText.tsx:364).

**Только Jest:** под `<React.StrictMode>` declarative POST без пользовательского streamId дважды вызывает mock native fetch. Первый effect начинает запрос, cleanup отменяет его и удаляет entry, повторный effect запускает второй. Дедупликация применяется только при `!owned`. Имеющийся StrictMode-тест проверяет именованный запрос.

**На обоих устройствах не подтверждено:** сервер получил ровно 1 POST, контрольный useEffect показал setup=1, cleanup=0. Вложенный StrictMode текущего RN 0.86/harness не выполнил replay. Изменять ReactFabric ради получения желаемого результата не стали. Это остаётся условным риском при рантайме с replay, а не доказанным багом проверенного приложения; прежняя формулировка была слишком сильной.

**Исправление:** определить replay-safe lifecycle запроса и гарантию для mutation requests; при необходимости применять явный idempotency key на сервере. Простое скрытие повторного effect флагом может оставить компонент с отменённым первым запросом.

**Регрессия:** named и owned GET/POST в StrictMode; после replay должен остаться один рабочий поток, а серверный счётчик side effects не должен увеличиваться дважды.

### R5 P2 Защита от повторного POST истекает без уведомления

Код: [evictLocked:152](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/cpp/StreamTextStreams.cpp:152), [useStreamRequest:366](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/src/StreamText.tsx:366), [README:167](/Users/alex/WebstormProjects/react-native-text-stream/README.md:167).

**Подтверждено кодом и существующим native test:** при превышении 4096 записей о вытесненных запросах удаляется примерно старшая четверть. Старый POST после этого не распознаётся `hasStream(..., includeEvicted=true)` и снова отправляется при remount без explicit release. README обещает, что вытеснённый POST не отправляется повторно.

**Подтверждено HTTP-сервером на iOS и Android:** первый declarative POST → unmount → 4161 завершённый declarative named GET `/empty` → remount исходного POST. На обеих платформах count вырос с 1 до 2; response text содержит второй side effect. Использованы реальный fetcher, нативный registry и Fabric, без mock транспорта.

Ограниченный размер кэша — намеренный компромисс, но его нельзя представлять как бессрочную защиту от повторного side effect. Native test уже проверяет забывание старого ключа; JS-mock использует неограниченный Map и не проверяет итоговую повторную отправку.

**Исправление:** явно ограничить гарантию в API и документации либо вынести устойчивую дедупликацию опасных операций на уровень приложения/сервера.

**Регрессия:** более 4160 последовательных завершённых именованных потоков, затем remount самого старого POST; проверить фактическое число запросов, а не только размер registry.

### R6 P2 Потерянная Fabric команда оставляет buffer в глобальном registry

Код: [StreamTextBuffers.cpp:10](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/cpp/StreamTextBuffers.cpp:10), [StreamText.tsx:223](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/src/StreamText.tsx:223).

**По коду:** `Buffers.put(buffer)` кладёт сильную ссылку в глобальную map, а удалить её может только `take(id)` при получении attach-команды. Ни discard, ни owner cleanup, ни ограничений ожидания нет. В установленном RN [SurfaceMountingManager.kt:739](/Users/alex/WebstormProjects/react-native-text-stream/node_modules/react-native/ReactAndroid/src/main/java/com/facebook/react/fabric/mounting/SurfaceMountingManager.kt:739) команда молча возвращается при остановленной surface; отсутствующий view тоже не получает её. Поэтому `put/queue attach → stop surface до исполнения batch → пропуск команды без take` оставляет buffer в registry. Явного cleanup этого handoff при остановке surface не найдено; device-проверка ниже измеряет удержание через 500 мс.

**Подтверждено на отдельном Android-эмуляторе:** тестовый счётчик registry перед attach показывает `{entries:0,bytes:0}`. Публичный ref.attach настоящего нативного буфера 4 МиБ даёт `{entries:1,bytes:4194368}`. Затем вызывается реальный `FabricUIManager.stopSurface(1)`. После 500 мс registry всё ещё содержит тот же entry; нативный лог подтверждает остановку surface и пропуск mount batch. [Сводка доказательства](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/android-r6-harness.log). Здесь нет поддельного `put` без dispatch: выполнен настоящий public ref/Fabric путь. Диагностический счётчик собран только с `STREAMTEXT_TESTING`.

Измерено удержание сильной ссылки и размера буфера, а не прирост RSS. iOS teardown этим device-тестом не проверялся; общий C++ registry тот же.

Сырые артефакты: [harness output](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/android-r6-raw-harness.log), [logcat с timestamp/PID](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/android-r6-logcat.log). Проба — [registry-retention.harness.tsx](/Users/alex/WebstormProjects/react-native-text-stream/example/__tests__/registry-retention.harness.tsx). Выполнять отдельно и последней: она намеренно останавливает свою Fabric surface.

**Исправление:** handoff должен иметь владельца и отмену/acknowledgement, а teardown очищать его незабранные entries. Не вводить произвольный TTL, способный удалить ещё ожидающую допустимую команду.

**Регрессия:** приостановить command dispatch между put и take, unmount/stop surface, затем проверить weak references и размер registry. Повторить с успешной доставкой и быстрыми многократными attach.

### R7 P2 Отмена фонового рисования iOS содержит гонку данных

Код: [DrawToken:561](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/ios/fabric/StreamTextPreparedLayout.swift:561), [cancelDrawing:747](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/ios/fabric/StreamTextPreparedLayout.swift:747), [drawAhead:759](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/ios/fabric/StreamTextPreparedLayout.swift:759).

**По коду:** обычный mutable Bool `cancelled` записывается на main thread и читается в `drawQueue` без синхронизации. `@unchecked Sendable` отключает проверку компилятора, но не делает поле атомарным. Комментарий о допустимом stale read не устанавливает межпоточную гарантию.

Финальная проверка перед установкой tile выполняется на main thread; поэтому показ старой картинки или crash этим ревью не доказан. Сам несинхронизированный доступ требует исправления.

**На симуляторе не доказано:** обычный `tiles.harness` прошёл два iOS-сценария с готовым длинным buffer и реальным canvas. Специальная сборка с Thread Sanitizer не дала запускаемого приложения: universal-конфигурация упёрлась в отсутствие `TSB_*` при Swift-компиляции x86_64, последняя arm64-попытка также не предоставила готовый executable для запуска. Это ограничение диагностической сборки, а не новая находка рендера. TSan warning для DrawToken не получен; наличие instrumented object само по себе не доказывает гонку во время исполнения. Временный stress hook удалён из production source; сохранён [patch диагностического stress-hook](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/r7-draw-token-tsan.patch), его применимость проверена через `git apply --check`. [Полный лог неудачной universal TSan-сборки](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/r7-tsan-universal-build.log).

**Исправление:** синхронизированный cancellation token либо другая схема без общего mutable поля между очередями.

**Регрессия:** TSan и scroll/theme/resize/unmount stress с ожидающими tile jobs; отдельно проверять, что старые jobs не устанавливают tiles нового поколения.

### R8 P2 TalkBack не проходит по словам с supplementary letters

Код: [StreamTextReading.kt:112](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/android/src/main/java/com/textstream/StreamTextReading.kt:112).

**Воспроизведено на production Kotlin-классе:** для одной буквы U+10400 Deseret вызовы `following(WORD, 0)` и `preceding(WORD, 2)` возвращают null вместо `[0,2]`. `isWordEnd()` вызывает `codePointAt(i-1)`, а i-1 указывает на младший surrogate. Аналогичные проверки предыдущего символа есть в word-start/backward логике.

**Подтверждено на Android API 35:** два instrumentation-теста production-класса дали `actual array was null`. [Полный лог](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/android-r8-instrumentation.log). Сам сервис TalkBack/озвучивание не запускался; доказан используемый им алгоритм границ слов на Android.

**Исправление:** назад двигаться по code points (`codePointBefore` и корректные offsets), сохраняя UTF-16 индексы для accessibility API.

**Регрессия:** supplementary letters и цифры, смешанные BMP/non-BMP слова, оба направления, offsets внутри surrogate pair и у границ. Имеющийся emoji-тест character traversal этот путь не проверяет. Исходники воспроизведения сохранены в [android-reading](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/android-reading).

### R9 P1 Смена streamId на пустой поток оставляет старое сообщение

Код: [Android setState:182](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/android/src/main/java/com/textstream/StreamTextView.kt:182), [Android setStreamId:201](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/android/src/main/java/com/textstream/StreamTextView.kt:201), [iOS updateProps:205](/Users/alex/WebstormProjects/react-native-text-stream/packages/react-native-text-stream/ios/fabric/StreamTextComponentView.mm:205).

**По коду, обе платформы:** view монтируется с `streamId=A` до публикации A; затем A приходит через native listener. В props source остаётся null. При смене id на B, под которым ещё нет buffer, новый prop source тоже null. iOS видит одинаковые `next.source/previous.source`, Android — одинаковые нулевые propSourceAddress, поэтому attach(null) не вызывается. Listener переключается на B, но прикреплённый A и его reader/observer остаются.

**Подтверждено на iOS и Android:** после смены A на пустой B native text и accessibilityValue остаются `previous message`, status=`done`, height=21 на iOS и ≈21.33 на Android. JS ref уже возвращает пустой текст и `idle`. Состояние не исправляется за 20 секунд. При переиспользовании bubble это отображение текста предыдущего сообщения под новым идентификатором. [Скриншот Android](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/android-r9.png), [скриншот iOS](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/ios-r9.png).

**Исправление:** смена stream identity должна явно отсоединять предыдущий источник, даже если оба prop pointers равны null. Затем attach текущего buffer B либо idle.

**Регрессия:** `mount A empty → publish A open → switch B empty → append/finish A`. На каждом шаге после switch B native text должен быть пустым, height=0 и status=idle. Затем publish B должен заполнить view ровно один раз. Повторить для `streamId=undefined`, release, списка с reuse и отсутствующего B после eviction.

## Пробелы в автоматических проверках

### G1 CI исключает большую часть renderer harness

[test-stream-fetch-harness.sh:26](/Users/alex/WebstormProjects/react-native-text-stream/scripts/test-stream-fetch-harness.sh:26) задаёт `(fetch|streamid).*harness`. В jobs iOS и Android именно этот runner используется для in-app tests. Поэтому `seams`, `ref`, `status`, `lifecycle`, `text`, `flow`, `prepared`, `resize`, `tiles`, `engines`, `utf8` не являются текущим CI gate. Наличие файлов этих suites не означает их запуск.

Нужно разделить быстрый сетевой job и обязательный renderer job, сохранять список реально запущенных tests/platform skips как artifact. Новый `StreamTextFlowTest.kt` в androidTest также не запускается командой `testDebugUnitTest`: нужен instrumentation task на emulator.

### G2 Моки скрывают переходы между слоями

Основные JS-тесты заменяют native host, view commands и весь TurboModule. Они полезны для JS-контракта, но не обнаружат native listener, несовпадение Yoga-height и показанного source, потерю Fabric command, race tile jobs или различия реальной доставки callback. Source-level contract tests сопоставляют имена/поля; они не доказывают lifecycle и память.

В native harness используется минимальная JSI-заглушка, а Foundation delegate проверяется вне RN runtime. Нужны отдельные реальные Hermes/Fabric/JNI сценарии reload, teardown, publish/unlisten и late callbacks. ASan/UBSan не заменяют TSan.

## Матрица приоритетных тестов на стыках

| Стык | Сценарий | Проверяемый инвариант |
| --- | --- | --- |
| JS source ownership ↔ native view | Все переходы source, streamId, attach, clear, request | Один владелец; одинаковые текст, serial, статус и высота |
| Registry ↔ Fabric command | put, остановка surface до take | Нет удерживаемого buffer после teardown |
| React lifecycle ↔ HTTP | StrictMode, abort до старта, remount, release, 4096+ eviction | Нет неявного повторного mutation; promises/events завершаются |
| Producer ↔ TSB1 ↔ renderer | Overflow, поздний маленький chunk, terminal split UTF-8 | Terminal и committed неизменны; итог — допустимый префикс |
| Native stream publication ↔ JS queue | Заблокировать JS, несколько replace/abort/release, затем flush | Старые callback не возвращают предыдущий source/status |
| Prepared layout ↔ Yoga | Дробная ширина у wrap threshold, resize после attach/clear | Cache on/off и streaming/finished пути дают одинаковую геометрию |
| Unicode ↔ layout ↔ accessibility | RTL, CJK, combining marks, ZWJ, non-BMP letters | Границы строк и навигация не повреждают grapheme/code point |
| Tile jobs ↔ lifecycle | Scroll, theme, density, memory warning, unmount | Нет data race, stale tiles и неограниченного удержания jobs |
| streamFetch API ↔ wire | URL, method, string body, UTF-8/NUL, headers, timeout, redirects | Сервер получает заданные параметры; response/done и terminal status согласованы |
| Package ↔ чистое приложение | npm pack, установка без workspace symlinks, iOS/Android Release | Все native/codegen файлы попадают в пакет и линкуются |

## Усиления и оставшиеся риски

Эти пункты не посчитаны отдельными подтверждёнными багами:

- **Валидация внешних буферов.** iOS renderer проверяет magic/version/capacity, но не `headerSize` перед использованием фиксированного offset 64. Текущие producers корректны; добавить malformed-header/native-buffer fuzzing и выровнять правила с native readBuffer.
- **Общие ресурсные бюджеты.** Per-buffer capacity и кэш завершённых streams не ограничивают суммарный объём активных/наблюдаемых потоков и выполняющихся layout jobs. Нужны измерения peak RSS, backpressure/лимиты concurrency и длительные сессии, прежде чем обещать устойчивость к любым размерам.
- **Цена больших snapshots.** `getText()` декодирует весь накопленный префикс, а request normalization/key сериализуют body и headers на render. Измерить реальные длинные prompts и частые parent renders; не объявлять это bottleneck без профиля.
- **Точность отображения.** Терминальный buffer status и callback о завершении обработки текста сами по себе не доказывают физическую презентацию кадра. Для обещаний «уже на экране» нужны frame/presentation измерения, а не только snapshots.
- **Сетевые границы.** Добавить реальные TLS/DNS failures, redirect loop, cross-origin Authorization/cookies, background/foreground и offline recovery на обеих платформах. Различия платформ документировать, не маскировать чрезмерно широкими expected outcomes.
- **Дедупликация mutation requests.** In-memory hash/tombstone cache не обеспечивает exactly-once. Для платных/необратимых действий нужен server-side idempotency contract.

## Порядок исправлений

1. Закрыть показ предыдущего сообщения после смены streamId R9.
2. Исправить общую модель источников R1/R2 и cancelled owned request R3. R4 держать отдельной проверкой при смене React/RN или условий replay, не текущим merge blocker.
3. Устранить registry retention R6 и data race R7; восстановить accessibility R8; согласовать гарантию дедупликации R5.
4. Включить renderer/seam suites в CI, добавить воспроизведения как постоянные regressions после исправлений. Устаревшие сценарии со старым producer адаптировать к собственному streamFetch.
5. Затем проверять Release, реальные устройства, память и производительность на репрезентативных нагрузках.

## Запуск сохранённых воспроизведений

Из корня репозитория:

```sh
rtk proxy sh reviews/2026-10-13/run-react-probes.sh
```

React runner временно помещает probe в существующий test root, отказывается перезаписывать существующий файл и удаляет только свою копию при выходе. На проверенной реализации ожидается exit code 1 и четыре failed assertions. Обычный test suite не содержит этих диагностических failures.

Для device proofs сначала собрать актуальный Debug app и запустить выделенные устройства. Из корня запустить fixture:

```sh
rtk proxy node reviews/2026-10-13/device-proof/server.mjs
```

Из `example` запустить iOS (путь приложения задать при первой установке):

```sh
rtk proxy env HARNESS_REVIEW_PROOFS=1 HARNESS_IOS_DEVICE="TextStream Review 17 Pro Max" HARNESS_IOS_VERSION=26.5 HARNESS_TEST_TIMEOUT=240000 HARNESS_METRO_PORT=18081 HARNESS_APP_PATH=/Users/alex/WebstormProjects/react-native-text-stream/example/ios/build/Build/Products/Debug-iphonesimulator/TextStreamExample.app bun x --no-install react-native-harness --config jest.harness.config.mjs --harnessRunner ios --watchman=false --testPathPatterns=review-repro.harness
```

Android: сначала подключить к fixture именно выделенный emulator, затем выполнить из `example`:

```sh
rtk proxy /Users/alex/Library/Android/sdk/platform-tools/adb -s emulator-5554 reverse tcp:19878 tcp:19878
rtk proxy env HARNESS_REVIEW_PROOFS=1 HARNESS_ANDROID_AVD=textstream_review_api35 HARNESS_TEST_TIMEOUT=240000 HARNESS_METRO_PORT=18082 HARNESS_APP_PATH=/Users/alex/WebstormProjects/react-native-text-stream/example/android/app/build/outputs/apk/debug/app-debug.apk bun x --no-install react-native-harness --config jest.harness.config.mjs --harnessRunner android --watchman=false --testPathPatterns=review-repro.harness
```

Диагностические `review-repro` и `registry-retention` исключены из обычного harness-запуска. Переменная `HARNESS_REVIEW_PROOFS=1` включает их явно; ожидаемые падения не смешиваются с регулярными проверками.

R6 выполнить отдельно и последним, из `example`, в Debug-сборке с `STREAMTEXT_TESTING` (в example этот флаг включён):

```sh
rtk proxy env HARNESS_REVIEW_PROOFS=1 HARNESS_ANDROID_AVD=textstream_review_api35 HARNESS_TEST_TIMEOUT=15000 HARNESS_METRO_PORT=18082 HARNESS_APP_PATH=/Users/alex/WebstormProjects/react-native-text-stream/example/android/app/build/outputs/apk/debug/app-debug.apk bun x --no-install react-native-harness --config jest.harness.config.mjs --harnessRunner android --watchman=false --testPathPatterns=registry-retention.harness
```

R8 отдельно запускается из корня после сборки и установки androidTest APK:

```sh
rtk proxy /Users/alex/Library/Android/sdk/platform-tools/adb -s emulator-5554 shell am instrument -w -r -e class com.textstream.StreamTextReadingSupplementaryDeviceTest com.textstream.test/androidx.test.runner.AndroidJUnitRunner
```

Fixture сохраняет `evidence.jsonl`; [manifest](/Users/alex/WebstormProjects/react-native-text-stream/reviews/2026-10-13/device-proof/manifest.json) содержит устройства и SHA-256 ключевых исходников. Не редактировать Metro-зависимости во время suite: Fast Refresh обрывает доказательный прогон. Harness-платформы запускать последовательно, поскольку они используют общий manifest.
