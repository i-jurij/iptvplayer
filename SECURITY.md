# Security

Thank you for caring about the security of this project! Below is information
on how to verify package authenticity and report vulnerabilities.

---

## Table of Contents

1. [Verifying Package Integrity](#verifying-package-integrity)
2. [Author's GPG Key](#authors-gpg-key)
3. [Reporting Vulnerabilities](#reporting-vulnerabilities)

---

## Verifying Package Integrity

Every release on GitHub contains a **checksums file** (`checksums.txt`) and its
**detached signature** (`checksums.txt.asc`). This is the universal, always-present
way to verify that files were not tampered with after building.

Individual packages may *additionally* carry their own signatures:

| Artifact | Primary signature | Fallback |
| :--- | :--- | :--- |
| `.deb` | Embedded, via `debsigs` (verify with `debsigs --verify <file>`) | Detached `<file>.asc` |
| `.rpm` | Embedded, via `rpm --addsign` (verify with `rpm --checksig <file>`) | Detached `<file>.asc` |
| `.AppImage` | — | Always detached `<file>.asc` |
| `checksums.txt` | — | Always detached `checksums.txt.asc` |

If a detached `.asc` is present in the release, `debsigs`/`rpm --addsign`
either did not run or failed; both mechanisms are considered authoritative.

`public-key.asc` is attached to each release. It is **not** listed inside
`checksums.txt` (the checksums are generated before the key export step), so
its integrity is not covered by `checksums.txt` — you should obtain it from
the release assets, or independently from the repository.

### What to download

For each release:

- **`checksums.txt`** — SHA256 hashes of the artifacts.
- **`checksums.txt.asc`** — detached signature of `checksums.txt`.
- **`public-key.asc`** — the author's public key (also mirrored in the repo).
- The packages you need (`.deb`, `.rpm`, `.AppImage`).

You do not need to download every artifact just to verify the one you care
about — see the note in "Verifying integrity" below.

---

### Importing the GPG key

The author signs releases with the following GPG key:

- **Email:** `mnisjil@duck.com`
- **Key ID:** `F01A34CBE2DFC712`
- **Fingerprint:** `17B1 75F4 AB16 6271 3D54 037C F01A 34CB E2DF C712`

The key is available:

- in each release as `public-key.asc` (recommended — it is what the CI exports);
- in the repository root as `pubkey.asc`.

Import the key:

<pre>
gpg --import public-key.asc
</pre>

If the key is already imported, you will see a "not changed" message.

**Before trusting the key, compare the fingerprint** printed by `gpg --import`
against the one above.

---

### Verifying integrity

Run the following in the directory that contains the downloaded files.

1. **Verify the signature of `checksums.txt`:**

<pre>
gpg --verify checksums.txt.asc checksums.txt
</pre>

Expected output (abridged — the "using ..." line shows the signing key,
whose last 16 hex characters must match the Key ID above):

<pre>
gpg: Signature made ...
gpg:                using RSA key 17B175F4AB1662713D54037CF01A34CBE2DFC712
gpg: Good signature from "ijurij &lt;mnisjil@duck.com&gt;"
</pre>

If you see `Good signature` and the key ID matches, the checksums file is
authentic.

1. **Verify the hashes of the artifacts you downloaded.**

If you downloaded **all** files listed in `checksums.txt`:

<pre>
sha256sum -c checksums.txt
</pre>

Every line must end with `OK`. Lines ending in `FAILED` mean the file is
corrupt or has been modified. If some files are missing, the command will
report `FAILED open or read` for them — that is expected, see the next point.

If you downloaded **only some** of the files (typical case), filter the
checksums file first:

<pre>
grep -E 'iptvplayer_0\.0\.21_amd64\.deb' checksums.txt | sha256sum -c -
</pre>

Or check a single file manually and compare the printed hash against the
matching line in `checksums.txt`:

<pre>
sha256sum filename
</pre>

1. **Optional — verify an inline package signature.**

`.deb` (if `debsigs` was used):

<pre>
debsigs --verify iptvplayer_*.deb
</pre>

`.rpm` (if `rpm --addsign` was used):

<pre>
rpm --checksig iptvplayer-*.rpm
</pre>

`.AppImage` (detached `.asc`):

<pre>
gpg --verify iptvplayer-linux-*.AppImage.asc iptvplayer-linux-*.AppImage
</pre>

Note: `.deb.asc` / `.rpm.asc` / `.AppImage.asc` files, when present, are
themselves listed in `checksums.txt`, so step 1+2 already covers them
indirectly.

---

### If the key is not trusted

The warning

<pre>
gpg: WARNING: This key is not certified with a trusted signature!
</pre>

is expected — you have not yet assigned trust to the key locally. This does
**not** mean the signature is invalid; it means gpg cannot vouch for the key's
identity via your own web of trust. To suppress the warning, assign ultimate
trust to the key:

<pre>
gpg --edit-key mnisjil@duck.com
> trust
> 5  # ultimate
> save
</pre>

Re-running the verification will now show `Good signature` without the
warning.

---

## Reporting Vulnerabilities

If you find a vulnerability in the code or infrastructure — **do not open a
public issue**. Contact me directly.

- **Email:** `mnisjil@duck.com`
- **Encryption:** please encrypt your message with my GPG key (see above).

We appreciate your contribution and guarantee:

- Quick response
- Acknowledgment in `README.md` or `CREDITS.md`
- Fix in the next release

---

## Recommendations for users

- Verify the signature of `checksums.txt` and the hashes before running.
- Compare the key fingerprint against the one published in this file.
- Download releases only from the official GitHub repository.
- Update to the latest version.

🔐 Security is everyone's responsibility.

---

---

# 🔐 Безопасность

Благодарим, что заботитесь о безопасности проекта! Ниже — информация о том,
как проверять подлинность пакетов и сообщать об уязвимостях.

---

## 📝 Содержание

1. [Проверка целостности пакетов](#-проверка-целостности-пакетов)
2. [GPG-ключ автора](#-gpg-ключ-автора)
3. [Сообщение об уязвимостях](#-сообщение-об-уязвимостях)

---

## 🔍 Проверка целостности пакетов

Каждый релиз на GitHub содержит **файл контрольных сумм** (`checksums.txt`) и
его **отделённую подпись** (`checksums.txt.asc`). Это универсальный и всегда
присутствующий способ убедиться, что файлы не были изменены после сборки.

Дополнительно сами пакеты могут нести собственную подпись:

| Артефакт | Основная подпись | Резервный вариант |
| :--- | :--- | :--- |
| `.deb` | Встроенная, через `debsigs` (проверка: `debsigs --verify <файл>`) | Отделённая `<файл>.asc` |
| `.rpm` | Встроенная, через `rpm --addsign` (проверка: `rpm --checksig <файл>`) | Отделённая `<файл>.asc` |
| `.AppImage` | — | Всегда отделённая `<файл>.asc` |
| `checksums.txt` | — | Всегда отделённая `checksums.txt.asc` |

Если в релизе присутствует отделённая `.asc`, значит `debsigs`/`rpm --addsign`
либо не запускались, либо завершились с ошибкой; оба механизма считаются
равнозначными.

`public-key.asc` прикладывается к каждому релизу. Он **не** входит в
`checksums.txt` (контрольные суммы генерируются до шага экспорта ключа),
поэтому его целостность через `checksums.txt` не проверяется — скачивайте
его из ассетов релиза либо берите из репозитория отдельно.

### 📥 Что нужно скачать

Для каждого релиза:

- **`checksums.txt`** — список SHA256-хешей артефактов.
- **`checksums.txt.asc`** — отделённая подпись `checksums.txt`.
- **`public-key.asc`** — открытый ключ автора (дублируется в репозитории).
- Пакеты, которые вам нужны (`.deb`, `.rpm`, `.AppImage`).

Скачивать все артефакты только ради проверки одного не требуется — см.
примечание в разделе «Проверка целостности».

---

### 🔑 Импорт GPG-ключа

Автор подписывает релизы с помощью GPG-ключа:

- **Email:** `mnisjil@duck.com`
- **Key ID:** `F01A34CBE2DFC712`
- **Fingerprint:** `17B1 75F4 AB16 6271 3D54 037C F01A 34CB E2DF C712`

Ключ доступен:

- в каждом релизе как `public-key.asc` (рекомендуется — именно этот файл
  экспортирует CI);
- в корне репозитория как `pubkey.asc`.

Импорт:

<pre>
gpg --import public-key.asc
</pre>

Если ключ уже есть, вы увидите сообщение, что он не изменился.

**Перед тем как доверять ключу, сверьте fingerprint**, который напечатает
`gpg --import`, со значением выше.

---

### ✅ Проверка целостности

Выполняйте команды в каталоге со скачанными файлами.

1. **Проверьте подпись `checksums.txt`:**

<pre>
gpg --verify checksums.txt.asc checksums.txt
</pre>

Ожидаемый вывод (сокращён — строка `using ...` содержит ключ подписи, его
последние 16 hex-символов должны совпадать с Key ID выше):

<pre>
gpg: Signature made ...
gpg:                using RSA key 17B175F4AB1662713D54037CF01A34CBE2DFC712
gpg: Good signature from "ijurij &lt;mnisjil@duck.com&gt;"
</pre>

Если вы видите `Good signature` и ключ совпадает — файл контрольных сумм
подлинен.

1. **Проверьте хеши скачанных артефактов.**

Если вы скачали **все** файлы, перечисленные в `checksums.txt`:

<pre>
sha256sum -c checksums.txt
</pre>

Каждая строка должна заканчиваться `OK`. Строки с `FAILED` означают порчу
или подмену файла. Если каких-то файлов нет, для них будет `FAILED open or
read` — это нормально, см. следующий пункт.

Если вы скачали **только часть** файлов (обычная ситуация), отфильтруйте
список:

<pre>
grep -E 'iptvplayer_0\.0\.21_amd64\.deb' checksums.txt | sha256sum -c -
</pre>

Или проверьте один файл вручную и сверьте напечатанный хеш со строкой в
`checksums.txt`:

<pre>
sha256sum имя-файла
</pre>

1. **Опционально — проверьте встроенную подпись пакета.**

`.deb` (если применялся `debsigs`):

<pre>
debsigs --verify iptvplayer_*.deb
</pre>

`.rpm` (если применялся `rpm --addsign`):

<pre>
rpm --checksig iptvplayer-*.rpm
</pre>

`.AppImage` (отделённая `.asc`):

<pre>
gpg --verify iptvplayer-linux-*.AppImage.asc iptvplayer-linux-*.AppImage
</pre>

Примечание: файлы `.deb.asc` / `.rpm.asc` / `.AppImage.asc`, если они есть,
сами перечислены в `checksums.txt` — то есть шаги 1 и 2 уже покрывают их
косвенно.

---

### 🔐 Если ключ не доверенный

Предупреждение

<pre>
gpg: WARNING: This key is not certified with a trusted signature!
</pre>

— ожидаемо: вы ещё не установили доверие к ключу локально. Это **не**
означает, что подпись недействительна; это означает, что gpg не может
подтвердить личность владельца ключа через вашу цепочку доверия. Чтобы
убрать предупреждение, установите максимальное доверие:

<pre>
gpg --edit-key mnisjil@duck.com
> trust
> 5  # ultimate
> save
</pre>

После этого повторная проверка покажет `Good signature` без предупреждения.

---

## 🛟 Сообщение об уязвимостях

Если вы нашли уязвимость в коде или инфраструктуре — **не создавайте
публичный issue**. Свяжитесь со мной напрямую.

- **Электронная почта:** `mnisjil@duck.com`
- **Шифрование:** зашифруйте письмо моим GPG-ключом (см. выше).

Мы ценим ваш вклад и гарантируем:

- Быстрый ответ
- Признание в `README.md` или `CREDITS.md`
- Исправление уязвимости в ближайшем релизе

---

## 🧩 Рекомендации пользователям

- Проверяйте подпись `checksums.txt` и хеши перед запуском.
- Сверяйте fingerprint ключа со значением, опубликованным в этом файле.
- Скачивайте релизы только с официального GitHub-репозитория.
- Обновляйтесь до последней версии.

🔐 Безопасность — это ответственность всех нас.
