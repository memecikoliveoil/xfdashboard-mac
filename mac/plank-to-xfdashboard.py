#!/usr/bin/env python3
"""Plank (Plank Reloaded) dock'undaki uygulamaları aynı sırayla xfdashboard'un
alt dock'una (quicklaunch favorileri) kopyalar. Plank'ta değişiklik yaptıkça
tekrar çalıştır. Plank'teki "Launchpad" öğesinin yerine xfdashboard'un kendi
uygulamalar düğmesi konur (Mac'teki gibi Finder'ın sağında olabilsin diye). Plank'taki ayraçlar (Separator docklet'i) da aynı
yerde xfdashboard dock'una ayraç olarak taşınır. Klasörler (İndirilenler gibi) ve
Çöp de taşınır; xfdashboard bunları Plank'teki gibi en sağda, açık
uygulamaların sağında tutar."""
import ast
import configparser
import subprocess
from pathlib import Path
from urllib.parse import unquote, urlparse

DOCK = Path.home() / ".config/plank/dock1"
LAUNCHERS = DOCK / "launchers"


def dock_items():
    # Plank Reloaded / yeni Plank: sıra gsettings'te
    for schema in ("net.launchpad.plank.dock.settings",):
        r = subprocess.run(["gsettings", "get",
                            f"{schema}:/net/launchpad/plank/docks/dock1/", "dock-items"],
                           capture_output=True, text=True)
        if r.returncode == 0 and "[" in r.stdout:
            txt = r.stdout.strip()
            if txt.startswith("@as"):
                txt = txt[3:].strip()
            items = [x for x in ast.literal_eval(txt) if x]
            if items:
                return items
    # Eski Plank: settings dosyası
    settings = DOCK / "settings"
    if settings.exists():
        for line in settings.read_text().splitlines():
            if line.startswith("DockItems="):
                return [x for x in line.split("=", 1)[1].split(";;") if x]
    return sorted(p.name for p in LAUNCHERS.glob("*.dockitem"))


SEPARATOR = "separator"
LAUNCHPAD = "launchpad"
DOCK_DIR = Path.home() / ".local/share/xfdashboard/dock"


def xdg_download_dir():
    try:
        r = subprocess.run(["xdg-user-dir", "DOWNLOAD"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip():
            return Path(r.stdout.strip()).resolve()
    except OSError:
        pass
    return None


def write_dock_entry(name, slug, icon, target):
    """xfdashboard'un açabileceği küçük bir .desktop dosyası yaz (Launchpad'de
    görünmez, sadece dock için)."""
    DOCK_DIR.mkdir(parents=True, exist_ok=True)
    f = DOCK_DIR / f"xfdashboard-dock-{slug}.desktop"
    f.write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={name}\n"
        f"Icon={icon}\n"
        f"Exec=gio open \"{target}\"\n"
        "NoDisplay=true\n"
        "Terminal=false\n")
    return str(f)


def special_entry(item):
    """Plank'teki klasör ve Çöp öğeleri için dock girdisi (yoksa None)."""
    url = launcher_url(item)
    low = url.lower()
    if low.startswith("docklet://") and "trash" in low:
        return write_dock_entry("Çöp", "trash", "user-trash", "trash:///")
    if url.startswith("file://"):
        path = Path(unquote(urlparse(url).path))
        if path.is_dir():
            icon = "folder-download" if path.resolve() == xdg_download_dir() else "folder"
            import unicodedata
            ascii_name = unicodedata.normalize("NFKD", path.name).encode("ascii", "ignore").decode().lower()
            slug = "".join(c if c.isalnum() else "-" for c in ascii_name) or "folder"
            return write_dock_entry(path.name, "folder-" + slug, icon, path)
    return None


def launcher_url(item):
    f = LAUNCHERS / item
    if not f.exists():
        return ""
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(f)
    return cp.get("PlankDockItemPreferences", "Launcher", fallback="")


def is_separator(item):
    url = launcher_url(item).lower()
    return (url.startswith("docklet://") and "separator" in url) or \
        (not url.startswith("file://") and "separator" in item.lower())


def launcher_path(item):
    url = launcher_url(item)
    if not url.startswith("file://"):
        return None  # klasör/docklet öğeleri
    path = unquote(urlparse(url).path)
    return path if path.endswith(".desktop") and Path(path).exists() else None


def app_dirs():
    import os
    home = os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))
    dirs = [home] + os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":")
    dirs += [str(Path.home() / ".local/share/flatpak/exports/share"),
             "/var/lib/flatpak/exports/share"]
    return [Path(d) / "applications" for d in dirs if d]


def desktop_id(path):
    """Dosya yolunu masaüstü kimliğine çevir (ör. org.gnome.Nautilus.desktop).
    xfdashboard çalışan pencereleri kimlikle eşleştiriyor; tam yol verilirse
    açık uygulama dock'ta ikinci kez görünüyordu."""
    p = Path(path).resolve()
    for d in app_dirs():
        try:
            rel = p.relative_to(d.resolve())
        except (ValueError, OSError):
            continue
        return str(rel).replace("/", "-")
    return path


favs = []
for item in dock_items():
    if is_separator(item):
        # başa, art arda veya en sona ayraç koyma
        if favs and favs[-1] != SEPARATOR:
            favs.append(SEPARATOR)
        continue
    special = special_entry(item)
    if special:
        if special not in favs:
            favs.append(special)
        continue
    if "launchpad" in launcher_url(item).lower() or launcher_url(item).lower() == "docklet://applications":
        if LAUNCHPAD not in favs:
            favs.append(LAUNCHPAD)
        continue
    path = launcher_path(item)
    if not path:
        continue
    fav = desktop_id(path)
    if fav not in favs:
        favs.append(fav)

while favs and favs[-1] == SEPARATOR:
    favs.pop()

if not favs:
    raise SystemExit("Plank dock öğesi bulunamadı, hiçbir şey değiştirilmedi.")

cmd = ["xfconf-query", "-c", "xfdashboard", "-p", "/favourites", "-n", "-a"]
for p in favs:
    cmd += ["-t", "string", "-s", p]
subprocess.run(["xfconf-query", "-c", "xfdashboard", "-p", "/favourites", "-r"],
               stderr=subprocess.DEVNULL)
subprocess.run(cmd, check=True)
print("xfdashboard dock'u:")
for p in favs:
    if p == SEPARATOR:
        print("   ──────")
    elif p == LAUNCHPAD:
        print("   [Launchpad]")
    elif p.startswith(str(DOCK_DIR)):
        print("   " + Path(p).name + "   (en sağda kalır)")
    else:
        print("   " + Path(p).name)
