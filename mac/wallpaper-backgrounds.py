#!/usr/bin/env python3
"""Makes the background images of the xfdashboard-mac themes from the current
wallpaper (and skippy-xd's, if ~/.config/skippy-xd exists).

  wallpaper-backgrounds.py           make them once (--force: even if unchanged)
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


def generate(force=False):
    # One at a time: the watcher and a manual run must not write the same files
    XFD_DIR.mkdir(parents=True, exist_ok=True)
    with open(XFD_DIR / ".wallpaper-backgrounds.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        _generate(force)


def _generate(force):
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

    # Nothing to do if the same wallpaper, style, mode and screen size were already
    # made (e.g. at login, or a theme change that keeps light/dark): no work and no
    # xfdashboard restart. Also checks that the images are still there.
    style = q("-c", "xfce4-desktop", "-p", f"{base}/image-style") if base else ""
    key = f"{wp}|{os.stat(wp).st_mtime_ns}|{style}|{mode}|{w}x{h}|{BAND}\n"
    state = XFD_DIR / ".wallpaper-backgrounds.state"
    made = all((XFD_DIR / f"{n}.png").exists() for n in
               ("workspace-background", "background", "launchpad-background"))
    if not force and made and state.exists() and state.read_text() == key:
        return

    # All images in one ImageMagick run: the wallpaper is decoded once and every
    # image is made from copies in memory (mpr:). PNGs are written with light
    # compression: they are read locally, so speed matters more than size.
    #   workspace-background.png  wallpaper at screen size, scaled like the desktop
    #                             (also skippy-xd's bg.png)
    #   background.png            the same with a blurred Spaces strip at the top
    #   launchpad-background.png  blurred, darker, same strip (applications/search)
    #   stdout                    average brightness of the strip, for its text color
    pid = os.getpid()
    out = {name: XFD_DIR / f"{name}.png" for name in
           ("workspace-background", "background", "launchpad-background")}
    tmp = {name: XFD_DIR / f"{name}.{pid}.tmp.png" for name in out}
    for p in out.values():
        if p.is_symlink():
            p.unlink()
    tint = ["-fill", "white", "-colorize", "40%"] if mode == "day" else ["-fill", "black", "-colorize", "25%"]
    dim = "18%" if mode == "day" else "34%"
    r = subprocess.run([
        "convert", "-define", "png:compression-level=1",
        # big JPEGs are decoded at about twice the screen size: much faster
        "-define", f"jpeg:size={2 * w}x{2 * h}", wp, *fit(base, w, h), "+repage",
        "-write", "mpr:screen", "-write", str(tmp["workspace-background"]), "+delete",
        # strip: blurred at a quarter size and scaled back (same look, far less work)
        "mpr:screen", "-crop", f"{w}x{BAND}+0+0", "+repage", "-virtual-pixel", "edge",
        "-resize", "25%", "-blur", "0x5", "-resize", f"{w}x{BAND}!", *tint,
        "-write", "mpr:band", "+delete",
        "mpr:screen", "mpr:band", "-gravity", "northwest", "-geometry", "+0+0", "-composite",
        "-write", str(tmp["background"]), "+delete",
        # Launchpad: shrink, blur, scale back, a bit more color, darker
        "mpr:screen", "-resize", "12%", "-blur", "0x6", "-resize", f"{w}x{h}!",
        "-modulate", "100,125", "-fill", "black", "-colorize", dim,
        "mpr:band", "-gravity", "northwest", "-geometry", "+0+0", "-composite",
        "-write", str(tmp["launchpad-background"]), "+delete",
        # brightness of the strip
        "mpr:band", "-colorspace", "Gray", "-resize", "1x1!", "-depth", "8", "gray:-",
    ], capture_output=True, cwd="/tmp")
    if r.returncode != 0 or not all(t.exists() for t in tmp.values()):
        for t in tmp.values():
            t.unlink(missing_ok=True)
        print(f"Could not convert: {wp}", file=sys.stderr)
        return
    for name in out:
        os.replace(tmp[name], out[name])  # never leave a half-written file
    if SKIPPY_BG.parent.is_dir():
        stmp = SKIPPY_BG.with_name(f"bg.{pid}.tmp.png")
        shutil.copyfile(out["workspace-background"], stmp)
        os.replace(stmp, SKIPPY_BG)

    # Text on the strip: dark on a light strip, white on a dark one
    lum = r.stdout[0] / 255.0 if r.stdout else (1.0 if mode == "day" else 0.0)
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
    state.write_text(key)
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
    watch() if "--watch" in sys.argv else generate(force="--force" in sys.argv)
