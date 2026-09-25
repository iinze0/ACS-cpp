# ACS-cpp — Air Crack Station

Native C++ / GTK desktop build of [ACS](https://github.com/iinze0/ACS).

[![release](https://img.shields.io/github/v/release/iinze0/ACS-cpp?style=flat-square)](https://github.com/iinze0/ACS-cpp/releases/latest)
Made by [Pakun](https://github.com/brazyqueso) & [iinze0](https://github.com/iinze0)

Monitor mode is handled by the app. Ships as an `amd64` `.deb`, launches with `sudo ACS-cpp`, and checks GitHub for a newer package on start.

## Family

| Repo | What you get |
|:-----|:-------------|
| **[ACS](https://github.com/iinze0/ACS)** | Shell station |
| **[ACS-app](https://github.com/iinze0/ACS-app)** | Python desktop app |
| **[ACS-cpp](https://github.com/iinze0/ACS-cpp)** | This repo — native C++ / GTK app |

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

Confirm the package:

```bash
dpkg -s acs-cpp | grep Version
```

You should see **v1.0.0** and **Made by Pakun & iinze0**.

Uninstall:

```bash
sudo apt purge acs-cpp
```

## Launch

```bash
sudo ACS-cpp
```

Also available from **Applications → ACS**.

## Build from source

The supported install is the `.deb` above. The `Makefile` in this repo builds the GTK binary (`bin/ACS-cpp`) if you have the GTK 3 dev headers and a working `pkg-config`.

## Disclaimer

Authorized lab and pentest use only. Only run this on networks you own or have written permission to test.

<p align="center">
  <a href="https://github.com/iinze0">iinze0</a> ·
  <a href="https://github.com/brazyqueso">Pakun</a> ·
  <a href="https://github.com/iinze0/ACS-cpp">ACS-cpp</a>
</p>
