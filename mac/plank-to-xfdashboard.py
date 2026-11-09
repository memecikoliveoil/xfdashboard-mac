#!/usr/bin/env python3
"""Copies the items of the Plank (Plank Reloaded) dock, in the same order, to
xfdashboard's dock (quicklaunch favourites). Run it again after changing Plank.

- Plank's "Launchpad" item becomes xfdashboard's own applications button, in
  the same place (so it can sit right of Finder like on macOS).
- Plank's separators (Separator docklet) become dividers in the same places.
- Folders (like Downloads) and the Trash are copied too; xfdashboard keeps them
  at the end, after the running applications, like Plank.

Dividers, the Launchpad position and folders at the end need a theme with
"plank-style: true;" (both xfdashboard-mac themes)."""
import ast
import configparser
import os
import subprocess
import unicodedata
from pathlib import Path
from urllib.parse import unquote, urlparse

DOCK = Path.home() / ".config/plank/dock1"
LAUNCHERS = DOCK / "launchers"

SEPARATOR = "separator"
LAUNCHPAD = "launchpad"
DOCK_DIR = Path.home() / ".local/share/xfdashboard/dock"


def trash_name():
    """Name of the Trash in the system language (Turkish or English)."""
    lang = os.environ.get("LC_ALL") or os.environ.get("LC_MESSAGES") or os.environ.get("LANG", "")
    return "Çöp" if lang.startswith("tr") else "Trash"


def dock_items():
    # Plank Reloaded / newer Plank: the order is in gsettings
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
    # Older Plank: settings file
    settings = DOCK / "settings"
    if settings.exists():
        for line in settings.read_text().splitlines():
            if line.startswith("DockItems="):
                return [x for x in line.split("=", 1)[1].split(";;") if x]
    return sorted(p.name for p in LAUNCHERS.glob("*.dockitem"))


def xdg_download_dir():
    try:
        r = subprocess.run(["xdg-user-dir", "DOWNLOAD"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip():
            return Path(r.stdout.strip()).resolve()
    except OSError:
        pass
    return None


def write_dock_entry(name, slug, icon, target):
    """Write a small .desktop file xfdashboard can open (hidden from the
    applications view, only for the dock)."""
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
    """Dock entry for Plank's folder and Trash items (None for anything else)."""
    url = launcher_url(item)
    low = url.lower()
    if low.startswith("docklet://") and "trash" in low:
        return write_dock_entry(trash_name(), "trash", "user-trash", "trash:///")
    if url.startswith("file://"):
        path = Path(unquote(urlparse(url).path))
        if path.is_dir():
            icon = "folder-download" if path.resolve() == xdg_download_dir() else "folder"
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
        return None  # folder/docklet items
    path = unquote(urlparse(url).path)
    return path if path.endswith(".desktop") and Path(path).exists() else None


def app_dirs():
    home = os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))
    dirs = [home] + os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":")
    dirs += [str(Path.home() / ".local/share/flatpak/exports/share"),
             "/var/lib/flatpak/exports/share"]
    return [Path(d) / "applications" for d in dirs if d]


def desktop_id(path):
    """Turn a file path into a desktop id (e.g. org.gnome.Nautilus.desktop).
    xfdashboard matches running windows by id; with a full path a running app
    showed up twice in the dock."""
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
        # no divider at the start, twice in a row or at the end
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
    raise SystemExit("No Plank dock items found, nothing changed.")

cmd = ["xfconf-query", "-c", "xfdashboard", "-p", "/favourites", "-n", "-a"]
for p in favs:
    cmd += ["-t", "string", "-s", p]
subprocess.run(["xfconf-query", "-c", "xfdashboard", "-p", "/favourites", "-r"],
               stderr=subprocess.DEVNULL)
subprocess.run(cmd, check=True)
print("xfdashboard dock:")
for p in favs:
    if p == SEPARATOR:
        print("   ──────")
    elif p == LAUNCHPAD:
        print("   [Launchpad]")
    elif p.startswith(str(DOCK_DIR)):
        print("   " + Path(p).name + "   (stays at the end)")
    else:
        print("   " + Path(p).name)
