# ACS — Air Crack Station

**C++ desktop app. Made by Pakun & iinze0**

## Install (use dpkg — apt /tmp will fail)

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

You should see **v1.0.0** and **Made by Pakun & iinze0**.

```bash
dpkg -s acs-cpp | grep Version
```

Uninstall: `sudo apt purge acs-cpp`

## Launch

```bash
sudo ACS-cpp
```

Also: **Applications → ACS**

Native GTK app. Scan turns monitor mode on by itself. Authorized lab / pentest use only. On launch it checks GitHub and installs a newer package by itself.

```
github.com/iinze0
github.com/iinze0/ACS-cpp
```
