#!/usr/bin/env python3
"""Makes the background images of the xfdashboard-mac themes from the current
wallpaper (and skippy-xd's, if ~/.config/skippy-xd exists).

  wallpaper-backgrounds.py           make them once
  wallpaper-backgrounds.py --watch   remake them whenever the wallpaper or the
                                     light/dark GTK theme changes (start it at login)

Works however the wallpaper is changed (Xfce's desktop settings, a script...):
it watches Xfce's desktop settings. Light or dark comes from the GTK theme's
name (dark if it contains "Dark"), and picks xfdashboard-mac or -mac-dark."""
import fcntl
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

os.environ.setdefault("DISPLAY", ":0")
os.environ["PATH"] = f"{Path.home()}/.local/bin:/usr/local/bin:" + os.environ.get("PATH", "/usr/bin:/bin")

BAND = 164  # height of the Spaces strip in pixels; must match xfdashboard-mac-kur.sh
SKIPPY_BG = Path.home() / ".config/skippy-xd/bg.png"
XFD_DIR = Path.home() / ".config/xfdashboard"


def q(*args):
    r = subprocess.run(["xfconf-query", *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def current_wallpaper():
    """Current wallpaper: the built-in screen's first workspace, else any."""
    keys = q("-c", "xfce4-desktop", "-l").splitlines()
    keys = [k for k in keys if k.endswith("/last-image")]
    keys.sort(key=lambda k: (not k.endswith("workspace0/last-image"), "monitoreDP" not in k))
    for k in keys:
        p = q("-c", "xfce4-desktop", "-p", k)
        if p and Path(p).is_file():
            return p, k.rsplit("/", 1)[0]
    return None, None


def current_mode():
    return "night" if "dark" in q("-c", "xsettings", "-p", "/Net/ThemeName").lower() else "day"


def fit(base, w, h):
    """Same scaling as the desktop's image style (3=stretched, 4=scaled, else zoomed)."""
    style = q("-c", "xfce4-desktop", "-p", f"{base}/image-style") if base else ""
    if style == "3":
        return ["-resize", f"{w}x{h}!"]
    if style == "4":
        return ["-resize", f"{w}x{h}", "-background", "black", "-gravity", "center", "-extent", f"{w}x{h}"]
    return ["-resize", f"{w}x{h}^", "-gravity", "center", "-extent", f"{w}x{h}"]


def run(*cmd, **kw):
    return subprocess.run(list(cmd), check=False, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, **kw)


def generate():
    # One at a time: the watcher and a manual run must not write the same files
    XFD_DIR.mkdir(parents=True, exist_ok=True)
    with open(XFD_DIR / ".wallpaper-backgrounds.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        _generate()


def _generate():
    wp, base = current_wallpaper()
    if not wp:
        print("No wallpaper found", file=sys.stderr)
        return
    mode = current_mode()
    w, h = 1440, 900
    m = re.search(r"current (\d+) x (\d+)",
                  subprocess.run(["xrandr", "--current"], capture_output=True, text=True).stdout)
    if m:
        w, h = int(m.group(1)), int(m.group(2))

    # The wallpaper at screen size, scaled like the desktop. Also skippy-xd's
    # background (set "background = ~/.config/skippy-xd/bg.png" in its config)
    screen = XFD_DIR / "workspace-background.png"
    tmp = XFD_DIR / f"workspace-background.{os.getpid()}.tmp.png"
    # Big JPEGs are decoded at about twice the screen size: much faster on old machines
    if run("convert", "-define", f"jpeg:size={2 * w}x{2 * h}", wp,
           *fit(base, w, h), str(tmp)).returncode != 0:
        print(f"Could not convert: {wp}", file=sys.stderr)
        return
    os.replace(tmp, screen)  # never leave a half-written file
    if SKIPPY_BG.parent.is_dir():
        stmp = SKIPPY_BG.with_name(f"bg.{os.getpid()}.tmp.png")
        shutil.copyfile(screen, stmp)
        os.replace(stmp, SKIPPY_BG)

    # xfdashboard: blurred Spaces strip at the top (frosted light by day, a bit
    # darker by night)
    xfd_bg = XFD_DIR / "background.png"
    if xfd_bg.is_symlink():
        xfd_bg.unlink()
    tint = ["-fill", "white", "-colorize", "40%"] if mode == "day" else ["-fill", "black", "-colorize", "25%"]
    run("convert", str(screen),
        "(", "+clone", "-crop", f"{w}x{BAND}+0+0", "+repage", "-virtual-pixel", "edge",
        # blur at a quarter size and scale back: same look, a fraction of the time
        "-resize", "25%", "-blur", "0x5", "-resize", f"{w}x{BAND}!", *tint, ")",
        "-gravity", "northwest", "-geometry", "+0+0", "-composite", str(xfd_bg))

    # Applications/search: blurred and darkened like Launchpad, same strip on top
    dim = "18%" if mode == "day" else "34%"
    run("convert", str(screen), "-resize", "12%", "-blur", "0x6", "-resize", f"{w}x{h}!",
        "-modulate", "100,125", "-fill", "black", "-colorize", dim,
        "(", str(xfd_bg), "-crop", f"{w}x{BAND}+0+0", "+repage", ")",
        "-gravity", "northwest", "-geometry", "+0+0", "-composite",
        str(XFD_DIR / "launchpad-background.png"))

    # Text on the strip: dark on a light strip, white on a dark one
    lum = 1.0 if mode == "day" else 0.0
    r = subprocess.run(["convert", str(xfd_bg), "-crop", f"{w}x{BAND}+0+0", "+repage",
                        "-colorspace", "Gray", "-resize", "1x1!", "-depth", "8", "gray:-"],
                       capture_output=True, cwd="/tmp")
    if r.stdout:
        lum = r.stdout[0] / 255.0
    ink = "1d1d1f" if lum > 0.55 else "ffffff"
    hover = "00000018" if lum > 0.55 else "ffffff30"
    (XFD_DIR / "themes").mkdir(parents=True, exist_ok=True)
    (XFD_DIR / "themes" / "global.css").write_text(
        "/* written by wallpaper-backgrounds.py: text color for the Spaces strip */\n"
        f"XfdashboardLiveWorkspace XfdashboardButton.title {{ color: #{ink}ff; }}\n"
        f"#add-workspace {{ color: #{ink}c0; }}\n"
        f"#add-workspace:hover {{ color: #{ink}ff; background-fill-color: #{hover}; }}\n",
        encoding="utf-8")
    run("xfconf-query", "-c", "xfdashboard", "-p", "/theme", "-n", "-t", "string",
        "-s", "xfdashboard-mac" if mode == "day" else "xfdashboard-mac-dark")

    # xfdashboard reads its background only at startup: restart it if running
    if run("pgrep", "-x", "xfdashboard").returncode == 0:
        run("xfdashboard", "-q")
        time.sleep(0.5)
        subprocess.Popen(["xfdashboard", "-d"], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    print(f"Backgrounds updated ({'dark' if mode == 'night' else 'light'}): {wp}")


def watch():
    """Watch the wallpaper and the GTK theme. A burst of changes (several
    workspaces, theme and wallpaper together) is handled once."""
    pending = threading.Event()

    def monitor(channel, pattern):
        p = subprocess.Popen(["xfconf-query", "-c", channel, "-m"],
                             stdout=subprocess.PIPE, text=True)
        for line in p.stdout:
            if re.search(pattern, line):
                pending.set()

    threading.Thread(target=monitor, args=("xfce4-desktop", r"last-image|image-style"), daemon=True).start()
    threading.Thread(target=monitor, args=("xsettings", r"/Net/ThemeName"), daemon=True).start()
    generate()
    while True:
        pending.wait()
        time.sleep(0.4)       # let the burst finish
        pending.clear()
        generate()


if __name__ == "__main__":
    watch() if "--watch" in sys.argv else generate()
