# `.github/` — служебные файлы репозитория

Всё, что GitHub читает автоматически: workflow CI, composite actions,
Dockerfile для сборочного образа. Плюс эта справка — чтобы через
полгода вспомнить, что тут как устроено, не перечитывая YAML.

## Что лежит в этой папке

| Путь | Назначение |
| --- | --- |
| `workflows/` | GitHub Actions: релиз, smoke-тесты, обслуживание образа, Pages |
| `actions/import-gpg/` | Composite action: импорт GPG-ключа и self-test подписи |
| `actions/cleanup-gpg/` | Composite action: удаление временного GNUPGHOME |
| `docker/iptvplayer-arch-build.Containerfile` | Образ для CI-сборки sharun-AppImage |
| `README.md` | Этот файл |

## Карта workflow

| Файл | Триггер | Что делает |
| --- | --- | --- |
| `release.yml` | push в `VERSION`, `workflow_dispatch` | Оркестратор релиза: тег → Arch-образ → AppImage → native-пакеты → публикация |
| `native-packages.yml` | вызов из `release.yml`, `workflow_dispatch` | Сборка `.deb`/`.rpm` в контейнерах. Reusable |
| `rebuild-arch-image.yml` | cron (1 января/апреля/июля/октября, 04:00 UTC), push в Containerfile, `workflow_dispatch` | Проверка upstream-сигналов и пересборка Arch-образа при изменениях |
| `smoke-test.yml` | завершение `release.yml`, `workflow_dispatch` | Проверка артефактов релиза в 5 контейнерах (AppImage) + 4 (native) |
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

## Job `arch-image` — Arch-образ для AppImage

**Зачем:** sharun-AppImage нужен Arch-контейнер с debloated mesa.
Собирается один раз, живёт в GHCR (`ghcr.io/i-jurij/iptvplayer-arch-build`).

**Перед сборкой проверяет два сигнала:**

- `updated_at` релиза `continuous` в `pkgforge-dev/archlinux-pkgs-debloated`;
- SHA256 `get-debloated-pkgs.sh` из `pkgforge-dev/Anylinux-AppImages`.

**Что лежит в LABEL-ах образа:** те же два сигнала. При следующей
проверке читаются через `docker inspect` и сравниваются с текущими.

**Когда пересобирается:**

- образа в GHCR нет (первый прогон);
- сигналы изменились;
- в `workflow_dispatch` выставлен `rebuild_arch_image=true`.

**Если сигналы не изменились** — job логирует «up to date», образ
не пересобирается. Дальше `appimage` использует существующий.

**Квартальный cron** (`rebuild-arch-image.yml`) делает ровно ту же
проверку вне релиза, чтобы образ обновлялся, даже если релизы не выходят.

## Job `appimage` — sharun-AppImage

Работает **внутри** Arch-образа из `arch-image` (`container: image: ...`).
Шаги:

1. Кэш `third_party` по ключу `*-arch-appimage-third_party-<hash>`.
   Ключ привязан к Arch — Ubuntu-кэш не подходит.
2. `setup-deps.sh --yes --skip-system` — сборка wxWidgets/wxSQLite3.
   `--rebuild-deps` игнорирует кэш.
3. `build-release.sh --type release` — сборка бинарника.
4. `build-package.sh --sharun --yes` — упаковка AppImage через
   `quick-sharun.sh` (скачивается в момент сборки).
5. Артефакт `packages` (retention 3 дня) + лог.

**Update-info:** в `build-sharun.sh` экспортируется `UPINFO` (не
`UPDATE_INFORMATION` — quick-sharun читает именно `UPINFO`).
appimagetool вшивает строку обновления и генерирует `.zsync`.

## Job `native-packages` — .deb/.rpm

Reusable workflow. Матрица из `setup` job:

- `ubuntu-24.04` → `.deb`
- `debian-13` → `.deb`
- `rocky-10` → `.rpm`

Каждый — в своём контейнере, `--user root`, `env.PATH` задан явно
(иначе на Rocky `import-gpg` затирает PATH и bash не находится).

Артефакты `native-<target>` (retention 3 дня) — только `.deb`/`.rpm`
и их `.asc`. Подпись внутри job-а через `import-gpg` + `debsigs`/
`rpm --addsign`.

## Job `release` — публикация

1. Скачивает артефакты `packages` и `native-*` в `dist/`.
2. `import-gpg` → экспорт публичного ключа → `public-key.asc`.
3. `checksums.txt` по всем файлам, подпись → `checksums.txt.asc`.
4. Проверка, что есть хоть что-то. Если нет — релиз не создаётся.
5. `softprops/action-gh-release` с тегом `v<VERSION>` и файлами.

**Тело релиза** собирается в шаге `Compose release body` — таблица
с отметками о том, что собралось, и инструкция «какой пакет выбрать».

## `smoke-test.yml`

Запускается по завершении `release.yml` (любой исход, success/failure).
Резолвит тег из VERSION на том же SHA, ждёт появления релиза до
2 минут, потом:

- скачивает артефакты релиза в artifact `release-assets` (retention 1 день);
- `smoke-appimage` — 5 контейнеров (ubuntu:26.04, debian:13, fedora:44,
  rockylinux:10, archlinux) × 1 вариант (sharun);
- `smoke-native` — 4 контейнера (ubuntu:26.04 + debian:13 + rocky-10 +
  fedora:44), ставит пакет, проверяет `--version`, ресурсы, `ldd`,
  Xvfb-запуск 15 секунд.

## `update-release.yml`

Ручной ремонт. Берёт артефакты `native-*` из последнего успешного
`Native packages` (или указанного `native_run_id`), переподписывает
и заливает в указанный тег через `softprops`.

**Ограничение:** workflow-артефакты живут 3 дня. Если прогон
`Native packages` старше 3 дней, дозалить не получится — надо
перезапускать `release.yml` целиком.

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

## Что менять в каком случае

| Задача | Файл |
| --- | --- |
| Новая версия пакета в релиз | `VERSION` |
| Новый таргет native (.deb/.rpm) | `native-packages.yml` → `ALL`, `release.yml` → `targets` |
| Новый контейнер в smoke | `smoke-test.yml` → `matrix.container` |
| Правки тела релиза | `release.yml` → `Compose release body` |
| Правки Arch-образа | `docker/iptvplayer-arch-build.Containerfile` |
| Принудительная пересборка образа | `workflow_dispatch` → `rebuild_arch_image=true` |
| Принудительная пересборка third_party | `workflow_dispatch` → `rebuild_deps=true` |
