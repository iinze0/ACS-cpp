# ACS-cpp — Air Crack Station

Native C++ / GTK desktop build of **[ACS](https://github.com/iinze0/ACS)**.

[![release](https://img.shields.io/github/v/release/iinze0/ACS-cpp?style=flat-square)](https://github.com/iinze0/ACS-cpp/releases/latest)
[![license](https://img.shields.io/badge/license-MIT-0b7285?style=flat-square)](LICENSE)
[![lang](https://img.shields.io/badge/C%2B%2B-GTK%203-00599C?style=flat-square)](#install)

Made by [Pakun](https://github.com/brazyqueso) and [iinze0](https://github.com/iinze0)

Monitor mode is handled in-app. Ships as an `amd64` `.deb`, launches with `sudo ACS-cpp`, and checks GitHub for a newer package on start.

## Family

| Repo | Role |
|:-----|:-----|
| **[ACS](https://github.com/iinze0/ACS)** | Shell station |
| **[ACS-app](https://github.com/iinze0/ACS-app)** | Python desktop client |
| **[ACS-cpp](https://github.com/iinze0/ACS-cpp)** | This repo — native C++ / GTK client |

## Install

Use `dpkg`. Installing a local `.deb` from `/tmp` with `apt` will fail.

```bash
wget -O /tmp/acs-cpp.deb https://github.com/iinze0/ACS-cpp/releases/download/v1.0.0/acs-cpp_1.0.0_amd64.deb
sudo dpkg -i /tmp/acs-cpp.deb
sudo ACS-cpp
```

One-liner:

```bash
curl -fsSL https://raw.githubusercontent.com/iinze0/ACS-cpp/main/install-acs-cpp.sh | sudo bash
sudo ACS-cpp
```

```bash
dpkg -s acs-cpp | grep -E 'Version|Maintainer'
sudo apt purge acs-cpp    # uninstall
```

## Launch

```bash
sudo ACS-cpp
```

Also available from **Applications → ACS**.

## Build from source

The supported install is the `.deb` above. The `Makefile` builds `bin/ACS-cpp` if you have GTK 3 headers and a working `pkg-config`:

```bash
make
```

## Disclaimer

Authorized lab and pentest use only. Run this only on networks you own or have written permission to test.

---

<p align="center">
  <a href="https://github.com/iinze0">iinze0</a> ·
  <a href="https://github.com/brazyqueso">Pakun</a> ·
  <a href="LICENSE">MIT</a>
</p>
