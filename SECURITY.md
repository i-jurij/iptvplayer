# Security

Thank you for caring about the security of this project! Below is information on how to verify package authenticity and report vulnerabilities.

---

## Table of Contents

1. [Verifying Package Integrity](#verifying-package-integrity)
2. [Author's GPG Key](#authors-gpg-key)
3. [Reporting Vulnerabilities](#reporting-vulnerabilities)

---

## Verifying Package Integrity

All official releases (`.deb`, `.rpm`, `.AppImage`) are accompanied by a **checksums file** (`checksums.txt`) and its **digital signature** (`checksums.txt.asc`).  
This allows you to verify that the files have not been tampered with after building.

### What to download?

For each release, you will need:

- **`checksums.txt`** – a list of SHA256 hashes of all packages.
- **`checksums.txt.asc`** – the signature of this file, made with the author's GPG key.
- **`public-key.asc`** – the public GPG key (if you haven't imported it yet).

Also download the packages you need (`.deb`, `.rpm`, `.AppImage`).

---

### Importing the GPG key

The author signs releases with the following GPG key:

- **Email:** `mnisjil@duck.com`
- **Key ID:** `F01A34CBE2DFC712`
- **Fingerprint:** `17B1 75F4 AB16 6271 3D54 037C F01A 34CB E2DF C712`

You can download the public key from the repository:  
[pubkey.asc](https://github.com/i-jurij/iptvplayer/raw/main/pubkey.asc)  
(or from the release folder, if it is included).

Import the key:

<pre>
gpg --import pubkey.asc
</pre>

If the key already exists, you will see a message that it hasn't changed.

---

### Verifying integrity

Perform the following steps in the folder with the downloaded files:

1. **Verify the signature of `checksums.txt`**:

<pre>
gpg --verify checksums.txt.asc checksums.txt
</pre>

Expected output:

<pre>
gpg: Good signature from "ijurij &lt;mnisjil@duck.com&gt;"
</pre>

If you see this, the checksums file is authentic.

1. **Verify the hashes of all downloaded packages**:

<pre>
sha256sum -c checksums.txt
</pre>

This command will automatically check the hashes of all files listed in `checksums.txt` and report whether each passed.

If you downloaded only one package, you can manually check its hash:

<pre>
sha256sum filename
</pre>

and compare it with the corresponding line in `checksums.txt`.

---

### If the key is not trusted

If you see a warning `WARNING: This key is not certified with a trusted signature!`, that's normal – you haven't yet assigned trust to the key. To suppress the warning, run:

<pre>
gpg --edit-key mnisjil@duck.com
> trust
> 5  # ultimate
> save
</pre>

After that, re‑checking will show `Good signature` without warnings.

---

## Reporting Vulnerabilities

If you find a vulnerability in the code or infrastructure – **do not create a public issue**. Instead, contact me directly.

- **Email:** `mnisjil@duck.com`
- **Encryption:** please encrypt your message with my GPG key (see above).

We appreciate your contribution and guarantee:

- Quick response
- Acknowledgment in `README.md` or `CREDITS.md`
- Fix in the next release

---

## Recommendations for users

- Always verify the signature of `checksums.txt` and the hashes before running.
- Download releases only from the official GitHub repository.
- Update to the latest version.

🔐 Security is everyone's responsibility.

---

---

# 🔐 Безопасность

Благодарим, что заботитесь о безопасности проекта! Ниже — информация о том, как проверять подлинность пакетов и сообщать об уязвимостях.

---

## 📝 Содержание

1. [Проверка целостности пакетов](#-проверка-целостности-пакетов)
2. [GPG-ключ автора](#-gpg-ключ-автора)
3. [Сообщение об уязвимостях](#-сообщение-об-уязвимостях)

---

## 🔍 Проверка целостности пакетов

Все официальные релизы (`.deb`, `.rpm`, `.AppImage`) сопровождаются **файлом контрольных сумм** (`checksums.txt`) и его **цифровой подписью** (`checksums.txt.asc`).  
Это позволяет вам убедиться, что файлы не были изменены после сборки.

### 📥 Что нужно скачать?

Для каждого релиза вам понадобятся:

- **`checksums.txt`** – список SHA256-хешей всех пакетов.
- **`checksums.txt.asc`** – подпись этого файла, сделанная GPG-ключом автора.
- **`public-key.asc`** – открытый GPG-ключ (если вы его ещё не импортировали).

Также скачайте нужные вам пакеты (`.deb`, `.rpm`, `.AppImage`).

---

### 🔑 Импорт GPG-ключа

Автор подписывает релизы с помощью GPG-ключа:

- **Email:** `mnisjil@duck.com`
- **Key ID:** `F01A34CBE2DFC712`
- **Fingerprint:** `17B1 75F4 AB16 6271 3D54 037C F01A 34CB E2DF C712`

Вы можете скачать открытый ключ из репозитория:  
[pubkey.asc](https://github.com/i-jurij/iptvplayer/raw/main/pubkey.asc)  
(или из папки с релизом, если он там есть).

Импортируйте ключ:

<pre>
gpg --import pubkey.asc
</pre>

Если ключ уже есть, вы увидите сообщение, что он не изменился.

---

### ✅ Проверка целостности

Выполните следующие шаги в папке со скачанными файлами:

1. **Проверьте подпись `checksums.txt`**:

<pre>
gpg --verify checksums.txt.asc checksums.txt
</pre>

Ожидаемый вывод:

<pre>
gpg: Good signature from "ijurij &lt;mnisjil@duck.com&gt;"
</pre>

Если вы видите это – файл контрольных сумм подлинен.

1. **Проверьте хеши всех скачанных пакетов**:

<pre>
sha256sum -c checksums.txt
</pre>

Эта команда автоматически сверит хеши всех файлов, перечисленных в `checksums.txt`, и сообщит, прошла ли проверка для каждого из них.

Если вы скачали только один пакет, можете проверить его вручную:

<pre>
sha256sum имя-файла
</pre>

и сравнить с соответствующей строкой в `checksums.txt`.

---

### 🔐 Если ключ не доверенный

Если вы видите предупреждение `WARNING: This key is not certified with a trusted signature!`, это нормально – вы ещё не установили доверие к ключу. Чтобы подавить предупреждение, выполните:

<pre>
gpg --edit-key mnisjil@duck.com
> trust
> 5  # ultimate
> save
</pre>

После этого повторная проверка покажет `Good signature` без предупреждений.

---

## 🛟 Сообщение об уязвимостях

Если вы нашли уязвимость в коде или инфраструктуре – **не создавайте публичный issue**. Вместо этого свяжитесь со мной напрямую.

- **Электронная почта:** `mnisjil@duck.com`
- **Шифрование:** пожалуйста, зашифруйте письмо с помощью моего GPG-ключа (см. выше).

Мы ценим ваш вклад и гарантируем:

- Быстрый ответ
- Признание в `README.md` или `CREDITS.md`
- Исправление уязвимости в ближайшем релизе

---

## 🧩 Рекомендации пользователям

- Всегда проверяйте подпись `checksums.txt` и хеши перед запуском.
- Скачивайте релизы только с официального GitHub-репозитория.
- Обновляйтесь до последней версии.

🔐 Безопасность — это ответственность всех нас.
