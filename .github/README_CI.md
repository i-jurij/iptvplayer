# `.github/` — служебные файлы репозитория

Всё, что GitHub читает автоматически: workflow CI, composite actions,
Dockerfile для сборочного образа. Плюс эта справка — чтобы через
полгода вспомнить, что тут как устроено, не перечитывая YAML.

## Что лежит в этой папке

| Путь | Назначение |
| --- | --- |
| `workflows/` | GitHub Actions: релиз, smoke-тесты, обслуживание образов, Pages |
| `actions/import-gpg/` | Composite action: импорт GPG-ключа и self-test подписи |
| `actions/cleanup-gpg/` | Composite action: удаление временного GNUPGHOME |
| `docker/iptvplayer-arch-build.Containerfile` | Образ для сборки бинарника, sharun-AppImage и Arch-пакета |
| `README.md` | Этот файл |

## Карта workflow

| Файл | Триггер | Что делает |
| --- | --- | --- |
| `release.yml` | push в `VERSION`, `workflow_dispatch` | Оркестратор релиза: тег → Arch-образ → бинарник → AppImage + Arch-пакет + native `.deb`/`.rpm` → публикация |
| `native-packages.yml` | вызов из `release.yml`, `workflow_dispatch` | Сборка `.deb`/`.rpm` в контейнерах. Reusable |
| `rebuild-arch-image.yml` | cron (1 янв/апр/июл/окт, 04:00 UTC), push в Containerfile, `workflow_dispatch` | Проверка upstream-сигналов и пересборка Arch-образа при изменениях |
| `smoke-test.yml` | завершение `release.yml`, `workflow_dispatch` | Проверка артефактов релиза: 5 контейнеров AppImage + 5 native (deb/rpm/arch) |
| `update-release.yml` | `workflow_dispatch` | Дозаливка/переподпись native-пакетов в существующий релиз |
| `docs-pages-site.yml` | push в `docs/`, `workflow_dispatch` | Публикация `docs/` на GitHub Pages |

## Как запускается релиз

1. Меняешь `VERSION` в репо → push в `main`.
2. `release.yml` → job `version`:
   - проверяет, что VERSION действительно изменился в этом push;
   - создаёт тег `v<VERSION>`, если его нет;
   - отдаёт `version` и `version_changed=true` остальным job-ам.
3. Всё остальное запускается **только если** `version_changed=true`.

Ручной запуск через `workflow_dispatch` форсирует `version_changed=true`.

## Job `arch-image` — Arch-образ

**Зачем:** sharun-AppImage, бинарник и Arch-пакет собираются в Arch-контейнере
с debloated mesa. Образ живёт в GHCR:
`ghcr.io/i-jurij/iptvplayer-arch-build`.

**Перед сборкой проверяет два сигнала:**

- `updated_at` релиза `continuous` в `pkgforge-dev/archlinux-pkgs-debloated`;
- SHA256 `get-debloated-pkgs.sh` из `pkgforge-dev/Anylinux-AppImages`.

**Что лежит в LABEL-ах образа:** те же два сигнала. При следующей
проверке читаются через `docker inspect` и сравниваются с текущими.

**Когда пересобирается:**

- образа в GHCR нет (первый прогон);
- сигналы изменились;
- в `workflow_dispatch` выставлен `rebuild_arch_image=true`.

**Если сигналы совпадают** — job логирует «up to date», образ
не пересобирается. Дальше все Arch-джобы используют существующий.

**Квартальный cron** (`rebuild-arch-image.yml`) делает ровно ту же
проверку вне релиза, чтобы образ обновлялся, даже если релизы не выходят.
Push в Containerfile форсирует безусловную пересборку.

## Job `arch-build` — бинарник

Единственное место, где собирается бинарник для Arch-ветки.
Собирает wxWidgets/wxSQLite3 в `third_party/` (с кэшем по
`*-arch-appimage-third_party-<hash>`), потом `install/` через
`build-release.sh`. Результат — артефакт `arch-install` (retention 3 дня).

`--rebuild-deps` в `workflow_dispatch` инвалидирует кэш `third_party`.

## Job `appimage` — sharun-AppImage

Скачивает `arch-install`, кладёт в `install/`, вызывает
`build-package.sh --sharun --yes`. Внутри работает `quick-sharun.sh`
(скачивается в момент сборки), который разворачивает все зависимости,
генерирует AppRun, .env и упаковывает через `appimagetool`.

**Update-info:** в `build-sharun.sh` экспортируется `UPINFO` (не
`UPDATE_INFORMATION` — quick-sharun читает именно `UPINFO`).
appimagetool вшивает строку обновления и генерирует `.zsync`.

Артефакт `packages` (retention 3 дня) + лог `appimage-build-log`.

## Job `arch-pkg` — `.pkg.tar.zst`

1. Скачивает `arch-install`.
2. **Автодетект зависимостей:** `ldd` по бинарнику → `pacman -Fq` по
   каждой библиотеке → список пакетов. Плюс `EXTRA_DEPS="mesa
   gdk-pixbuf2 librsvg"` для `dlopen`-зависимостей, которые `ldd`
   не видит. `glibc` и `gcc-libs` исключаются.
3. Генерирует `PKGBUILD` с посчитанными `depends`.
4. Собирает через `makepkg --nodeps` от пользователя `builder`.
5. Подписывает `.asc` и заливает артефакт `native-arch`.

`package()` копирует только `install/bin/` и `install/share/` — файлы
`VERSION*` остаются вне пакета.

## Job `native-packages` — `.deb`/`.rpm`

Reusable workflow. Матрица из `setup` job:

- `ubuntu-24.04` → `.deb`
- `debian-13` → `.deb`
- `rocky-10` → `.rpm`

Каждый — в своём контейнере, `--user root`, `env.PATH` задан явно
(иначе на Rocky `import-gpg` затирает PATH и bash не находится).

Артефакты `native-<target>` (retention 3 дня) — `.deb`/`.rpm` и их `.asc`.

## Job `release` — публикация

1. Скачивает артефакты `packages`, `native-*` (включая `native-arch`)
   в `dist/`.
2. `import-gpg` → экспорт публичного ключа → `public-key.asc`.
3. `checksums.txt` по всем файлам, подпись → `checksums.txt.asc`.
4. Проверка, что есть хоть что-то. Если нет — релиз не создаётся.
5. `softprops/action-gh-release` с тегом `v<VERSION>` и файлами.

**Тело релиза** собирается в шаге `Compose release body` — таблица
с отметками о том, что собралось, и инструкция «какой пакет выбрать».

## `smoke-test.yml`

Запускается по завершении `release.yml` (любой исход).
Резолвит тег из VERSION на том же SHA, ждёт релиз до 2 минут, потом:

- скачивает артефакты релиза в artifact `release-assets` (retention 1 день);
- `smoke-appimage` — 5 контейнеров (ubuntu:26.04, debian:13, fedora:44,
  rockylinux:10, archlinux) × 1 вариант (sharun);
- `smoke-native` — 5 контейнеров (ubuntu:26.04, debian:13, rocky-10,
  fedora:44, archlinux): ставит `.deb`/`.rpm`/`.pkg.tar.zst`, проверяет
  `--version`, ресурсы, `.desktop`, `ldd`, Xvfb-запуск 15 секунд.
  Для Arch дополнительно проверяется, что `VERSION*` не попали в корень.

## `update-release.yml`

Ручной ремонт. Берёт артефакты `native-*` (включая `native-arch`)
из последнего успешного `Native packages` (или указанного
`native_run_id`), переподписывает и заливает в указанный тег.

**Ограничение:** workflow-артефакты живут 3 дня. Если прогон старше
3 дней, дозалить не получится — надо перезапускать `release.yml` целиком.

## Composite actions

- `.github/actions/import-gpg` — импорт ключа, self-test подписи,
  создание wrapper-скрипта `gpg` с `--passphrase-file`, добавление
  wrapper-каталога в `GITHUB_PATH`. Если ключа/пароля нет —
  `can_sign=false` и выход до всего остального.
- `.github/actions/cleanup-gpg` — удаление `GNUPGHOME` и wrapper-каталога.

**Важно про PATH:** `import-gpg` дописывает свой каталог в
`GITHUB_PATH`. На образах без `PATH` в конфиге (Rocky) это затирает
системный PATH. Лечится `env.PATH` в `container:` job-а
(см. `native-packages.yml`).

## GHCR

- Образ: `ghcr.io/i-jurij/iptvplayer-arch-build:latest`.
- Права: `packages: write` для сборки, `packages: read` для использования.
- Приватность: по умолчанию приватный. Для локального `podman pull`
  нужен либо `podman login ghcr.io` с PAT (scope `read:packages`),
  либо сделать образ публичным в настройках пакета.

## Локальная сборка (не CI)

- `setup-deps.sh --yes` — системные пакеты + third_party.
- `build-release.sh` — бинарник.
- `build-package.sh --native-deb|--native-rpm` — нативные пакеты.
- `build-package.sh --appimage` — linuxdeploy-AppImage (только локально,
  в релиз не идёт).
- `build-package.sh --sharun` — sharun-AppImage (то же, что в CI, но
  на системной mesa, а не debloated).
- Arch `.pkg.tar.zst` локально не собирается — только в CI, через
  `makepkg` в Arch-образе. Локально его можно воспроизвести
  вручную: `podman run --rm -v "$PWD:/src:Z" -w /src
  ghcr.io/i-jurij/iptvplayer-arch-build:latest ./scripts/build-release.sh ...`
  потом `makepkg` вручную.

## Что менять в каком случае

| Задача | Файл |
| --- | --- |
| Новая версия пакета в релиз | `VERSION` |
| Новый таргет native (.deb/.rpm) | `native-packages.yml` → `ALL`, `release.yml` → `targets` |
| Новый контейнер в smoke | `smoke-test.yml` → `matrix.include` / `matrix.container` |
| Правки тела релиза | `release.yml` → `Compose release body` |
| Правки Arch-образа | `docker/iptvplayer-arch-build.Containerfile` |
| Список `EXTRA_DEPS` для Arch-пакета | `release.yml` → `arch-pkg` → `Detect runtime dependencies` |
| Принудительная пересборка образа | `workflow_dispatch` → `rebuild_arch_image=true` |
| Принудительная пересборка third_party | `workflow_dispatch` → `rebuild_deps=true` |
