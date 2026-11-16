# GScreen

Делит один большой монитор (Samsung Odyssey G9, 5120×1440) на 2–3 настоящих монитора Windows.
Окна разворачиваются на «свою» зону, работают `Win+Shift+←/→`, в OBS каждая зона — отдельный дисплей.
`Ctrl+Alt+G` возвращает цельный экран для игр: 240 Гц, G-Sync и HDR работают как обычно.

## Как это работает

```
приложения → виртуальные мониторы A/B/C (драйвер VDD, IddCx)
           → GScreen.Core: Desktop Duplication → D3D11 → один кадр 5120×1440
           → физический G9
```

| Часть | Язык | Что делает |
|---|---|---|
| `core/` → `GScreen.Core.exe` | C++20 | Драйвер VDD, раскладка дисплеев, компоновщик, трей, горячие клавиши, IPC |
| `core/src/Watchdog.cpp` → `GScreen.Core.exe --watchdog` | C++ | Второй процесс ядра без GPU: возвращает экран, если ядро упало или зависло (`Ctrl+Alt+Shift+R`) |
| `ui/GScreen.App` → `GScreen.App.exe` | C# / WinUI 3 | Окно настроек: пресеты, перетаскивание границ, режимы |

### Режимы вывода

* **Оконный** (Windows 11 Home / Pro — **ваш случай**). G9 остаётся монитором Windows, но уезжает в угол рабочего стола: он касается ряда зон только одной точкой. Поверх него открывается окно компоновщика на весь экран. Курсор не пускается на G9 (низкоуровневый хук мыши), а окна, попавшие туда, переносятся в зону A.
* **Монопольный** (Enterprise / Pro for Workstations). G9 убирается с рабочего стола («Удалить дисплей с рабочего стола»), и ядро выводит кадры напрямую через `Windows.Devices.Display.Core`. Windows разрешает это только при лицензии `Display-Specialized-Displays-Enabled = 1`. На Windows 11 Pro она равна 0, режим выбирается автоматически.
* **Предпросмотр** (`GScreen.Core.exe --preview`). Компоновка показывается в обычном окне, а G9 не трогается. Режим нужен для безопасной проверки.

## Что нужно установить

1. **Virtual Display Driver** (подписанный open-source драйвер, не требует тестового режима Windows, античиты не ругаются):
   ```
   winget install --id=VirtualDrivers.Virtual-Display-Driver -e
   ```
2. **Smart App Control.** Если он включён, Windows блокирует неподписанные программы. Готовые релизы GScreen подписываются через SignPath Foundation (см. «Code signing policy»). Программа, собранная локально через `build.cmd`, остаётся без подписи: при включённом Smart App Control её запуск будет заблокирован.

> **Что GScreen меняет в системе.** В режиме зон программа добавляет виртуальные мониторы, меняет раскладку дисплеев и основной монитор и правит `C:\VirtualDisplayDriver\vdd_settings.xml`. Перед первой правкой создаётся резервная копия `vdd_settings.xml.gscreen-backup`. Возврат в «Единый экран» отменяет изменения раскладки.

## Сборка

Нужны Visual Studio 2022 Build Tools (C++ и Windows SDK) и .NET 9 SDK.

```
build.cmd
```

Результат лежит в `out\GScreen\`. Запускать нужно `GScreen.App.exe`: окно само запустит ядро, а ядро — сторожевой процесс.

## Горячие клавиши

| Клавиши | Действие |
|---|---|
| `Ctrl+Alt+G` | Зоны ⇄ единый экран |
| `Ctrl+Alt+1…4` | Пресеты: три равные, фокус в центре, две половины, игра + чат |
| `Ctrl+Alt+Shift+R` | Аварийный возврат (работает, даже если ядро зависло) |

(`Win+Alt+G` из макета занят Xbox Game Bar, поэтому используется `Ctrl+Alt`.)

## Ограничения

* В режиме зон нет VRR/G-Sync и HDR, задержка больше примерно на 1 кадр (≈4 мс при 240 Гц).
* Видео с аппаратной защитой (Netflix и т. п.) на виртуальных мониторах может быть чёрным.
* Экраны входа и блокировки не делятся.
* При смене раскладки виртуальные мониторы переподключаются: экран мигает, окна переставляются.
* На OLED постоянные линии выгорают, поэтому разделители выключены (`showDividers` в `%APPDATA%\GScreen\config.json`).

Журнал ядра: `%APPDATA%\GScreen\core.log`.

## Удаление

1. Включите «Единый экран» (`Ctrl+Alt+G`) и закройте GScreen через меню в трее → «Выход».
2. Удалите папку с программой и `%APPDATA%\GScreen`.
3. Если вы меняли настройки драйвера только ради GScreen, верните резервную копию: переименуйте `C:\VirtualDisplayDriver\vdd_settings.xml.gscreen-backup` в `vdd_settings.xml`.
4. Если драйвер больше не нужен, удалите его: `winget uninstall --id=VirtualDrivers.Virtual-Display-Driver`.

## Релизы и подпись (для мейнтейнера)

Сборка и подпись идут в GitHub Actions ([.github/workflows/build.yml](.github/workflows/build.yml)). Каждый push собирает пакет и прогоняет самотесты. Подписываются только теги `vX.Y.Z`: в SignPath уходят четыре собственных файла GScreen (остальные DLL уже подписаны Microsoft), а подписанный пакет публикуется как GitHub Release.

Однократная настройка:
1. Публичный репозиторий на GitHub, у всех участников включена двухфакторная аутентификация.
2. Заявка в SignPath Foundation: <https://signpath.org/apply>.
3. В SignPath: добавить Trusted Build System «GitHub.com» и привязать его к проекту. Artifact configuration взять из [.signpath/artifact-configuration.xml](.signpath/artifact-configuration.xml).
4. В репозитории: секрет `SIGNPATH_API_TOKEN` и переменные `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`.
5. Релиз: поднять версию в `CMakeLists.txt` и `GScreen.App.csproj` (CI проверяет, что версии совпадают), затем `git tag v0.1.0 && git push --tags` и подтвердить запрос подписи в SignPath.

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

* **Signed files:** only `GScreen.Core.exe`, `GScreen.App.exe` and `GScreen.App.dll`, built from this repository by GitHub Actions. Third-party files in the package keep their original publishers' signatures.
* **Team roles:**
  * Committers and reviewers: [@EminGame5392](https://github.com/EminGame5392)
  * Approvers: [@EminGame5392](https://github.com/EminGame5392)
* **Approval:** every release is approved manually in SignPath before signing.
* **Privacy policy:** this program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. GScreen talks only to its own processes through a local named pipe and to the Virtual Display Driver on the same computer.

## License

[MIT](LICENSE)
