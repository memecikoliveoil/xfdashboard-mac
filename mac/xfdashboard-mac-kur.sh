#!/bin/sh
# Installs "xfdashboard-mac", a Mission Control-like theme based on xfdashboard-blue:
# - workspaces move from the right to a horizontal strip at the top, with names
# - the screen is darkened much less (the wallpaper stays visible)
# - the quicklaunch moves from the left to a horizontal dock at the bottom
# - window titles only show under the pointer
# Leaves the system themes alone; everything goes to ~/.themes/xfdashboard-mac(-dark).
set -e

# Preview screenshots live next to this script
# (screenshot-xfdashboard-mac.png, screenshot-xfdashboard-mac-dark.png)
HERE=$(cd "$(dirname "$0")" && pwd)

SRC=""
for d in /usr/local/share/themes/xfdashboard-blue/xfdashboard-1.0 \
         /usr/share/themes/xfdashboard-blue/xfdashboard-1.0 \
         "$HOME/.themes/xfdashboard-blue/xfdashboard-1.0" \
         "$HOME/.local/share/themes/xfdashboard-blue/xfdashboard-1.0"; do
    [ -d "$d" ] && SRC="$d" && break
done
[ -n "$SRC" ] || { echo "xfdashboard-blue theme not found" >&2; exit 1; }

DST="$HOME/.themes/xfdashboard-mac/xfdashboard-1.0"
rm -rf "$DST"
mkdir -p "$DST"
cp -r "$SRC"/. "$DST"/

# Theme name
# (translated lines like Name[tr]= must go, or the settings still show "xfdashboard-blue")
sed -i '/^Name\[/d; /^Comment\[/d;
        s/^Name=.*/Name=xfdashboard-mac/;
        s/^Comment=.*/Comment=macOS-like Mission Control with a Plank-like dock (light)/;
        s/^Author=.*/Author=Vaveylâ (memecikoliveoil)/;
        s/^Screenshot=.*/Screenshot=screenshot-xfdashboard-mac.png/' \
    "$DST/xfdashboard.theme"
rm -f "$DST"/screenshot-xfdashboard-blue.png
[ -f "$HERE/screenshot-xfdashboard-mac.png" ] && cp "$HERE/screenshot-xfdashboard-mac.png" "$DST/"

# Layout: workspace strip at the top, windows below, dock at the bottom
cat > "$DST/xfdashboard.xml" << 'EOF'
<interface>
	<object class="XfdashboardStageInterface" id="primary">
		<child>
			<object class="XfdashboardActor">
				<property name="x-expand">true</property>
				<property name="y-expand">true</property>
				<property name="x">0</property>
				<property name="y">0</property>

				<constraint>
					<object class="ClutterBindConstraint">
						<property name="coordinate">width</property>
						<property name="offset">0</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>

				<constraint>
					<object class="ClutterBindConstraint">
						<property name="coordinate">height</property>
						<property name="offset">-8</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>

				<layout>
					<object class="ClutterBoxLayout">
						<property name="orientation">vertical</property>
						<property name="spacing">8.0</property>
						<property name="homogeneous">false</property>
					</object>
				</layout>

				<!-- Spaces strip. The blurred, slightly lighter band behind it is part of the
				     background image (background.png, band height 164 pixels). -->
				<child>
					<object class="XfdashboardWorkspaceSelector" id="workspace-selector">
						<property name="can-focus">true</property>
						<property name="x-expand">false</property>
						<property name="x-align">center</property>
						<property name="orientation">horizontal</property>
						<property name="show-current-monitor-only">true</property>
					</object>
				</child>

				<!-- No view selector; the search box floats invisibly under the strip (below). -->

				<child>
					<object class="XfdashboardViewpad" id="viewpad">
						<property name="margin-top">8</property>
						<property name="margin-bottom">14</property>
						<property name="margin-left">20</property>
						<property name="margin-right">20</property>
						<property name="can-focus">true</property>
						<property name="x-expand">true</property>
						<property name="y-expand">true</property>
					</object>
				</child>

				<child>
					<object class="XfdashboardQuicklaunch" id="quicklaunch">
						<property name="can-focus">true</property>
						<property name="orientation">horizontal</property>
						<property name="x-expand">false</property>
						<property name="x-align">center</property>
					</object>
				</child>
			</object>
		</child>

		<!-- "+" in the top right corner: adds a workspace (add-workspace action of this fork) -->
		<child>
			<object class="XfdashboardActionButton" id="add-workspace">
				<property name="fixed-position-set">true</property>
				<property name="z-position">0.05</property>
				<property name="target">XfdashboardWorkspaceSelector</property>
				<property name="action">add-workspace</property>
				<property name="text">+</property>

				<constraint>
					<object class="ClutterAlignConstraint">
						<property name="align-axis">x-axis</property>
						<property name="factor">0.985</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>

				<constraint>
					<object class="ClutterBindConstraint">
						<property name="coordinate">y</property>
						<property name="offset">52</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>
			</object>
		</child>

		<child>
			<object class="XfdashboardTextBox" id="notification">
				<property name="fixed-position-set">true</property>
				<property name="z-position">0.1</property>
				<property name="request-mode">height-for-width</property>

				<constraint>
					<object class="ClutterAlignConstraint">
						<property name="align-axis">x-axis</property>
						<property name="factor">0.5</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>

				<constraint>
					<object class="ClutterAlignConstraint">
						<property name="align-axis">y-axis</property>
						<property name="factor">1.0</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>
			</object>
		</child>

		<!-- Invisible search: appears under the strip as soon as you type -->
		<child>
			<object class="XfdashboardTextBox" id="searchbox">
				<property name="fixed-position-set">true</property>
				<property name="z-position">0.05</property>
				<property name="can-focus">true</property>
				<property name="editable">true</property>
				<property name="width">360</property>

				<constraint>
					<object class="ClutterAlignConstraint">
						<property name="align-axis">x-axis</property>
						<property name="factor">0.5</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>

				<constraint>
					<object class="ClutterBindConstraint">
						<property name="coordinate">y</property>
						<property name="offset">178</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>
			</object>
		</child>

		<!-- Text in the middle when there are no windows (macOS: "No Available Windows") -->
		<child>
			<object class="XfdashboardButton" id="empty-windows">
				<property name="fixed-position-set">true</property>
				<property name="z-position">0.04</property>

				<constraint>
					<object class="ClutterAlignConstraint">
						<property name="align-axis">x-axis</property>
						<property name="factor">0.5</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>

				<constraint>
					<object class="ClutterAlignConstraint">
						<property name="align-axis">y-axis</property>
						<property name="factor">0.5</property>
						<property name="source" ref="primary"></property>
					</object>
				</constraint>
			</object>
		</child>

		<child>
			<object class="XfdashboardTextBox" id="tooltip">
				<property name="fixed-position-set">true</property>
				<property name="z-position">0.1</property>
				<property name="request-mode">height-for-width</property>
			</object>
		</child>
	</object>

	<focusables>
		<focus ref="viewpad" />
		<focus ref="searchbox" />
		<focus ref="workspace-selector" />
		<focus ref="quicklaunch" />
	</focusables>
</interface>
EOF

# Colors and looks: appended to blue's style sheet (later rules override earlier ones)
STYLE=$(sed -n 's/^Style=//p' "$DST/xfdashboard.theme" | cut -d';' -f1)
cat >> "$DST/$STYLE" << 'EOF'

/* ---- xfdashboard-mac ---- */

/* Dimming: blue uses #000000d0 (about 80% black). Keep only a light shade. */
XfdashboardStageInterface
{
	background-color: #00000000;
}

XfdashboardWorkspaceSelector
{
	orientation: horizontal;
	max-fraction: 0.19;
	spacing: 14.0;
	background-type: none;
	outline-width: 0.0;
}

/* "+" in the top right corner: adds a workspace */
#add-workspace
{
	button-style: text;
	font: "Inter 26";
	color: #ffffffd0;
	padding: 6.0;
	background-type: none;
}

#add-workspace:hover
{
	color: #ffffffff;
	background-type: fill rounded-corners;
	background-fill-color: #ffffff30;
	corner-radius: 8.0;
}

/* Workspace thumbnails: no frame, name BELOW the thumbnail (workspace-name-below,
 * this fork), blue outline around the current one */
XfdashboardLiveWorkspace
{
	show-workspace-name: true;
	workspace-name-below: true;
	workspace-name-padding: 5.0;
	background-type: none;
	outline-width: 0.0;
}

/* Current workspace: square thumbnail, so a square outline, wrapped tightly around
 * the outside (outline width = outset) */
XfdashboardLiveWorkspace:active
{
	background-type: outline;
	outline-color: #3d8af7ff;
	outline-width: 3.0;
	outline-outset: 3.0;
}

/* Workspace name: plain text, no background. Light/dark colors at the end of the file. */
XfdashboardLiveWorkspace XfdashboardButton.title
{
	button-style: text;
	background-type: none;
	font: "Inter Medium 10";
	color: #1d1d1fff;
}

/* Keep the search box and view selector unobtrusive */
#searchbox, #view-selector
{
	background-fill-color: #00000040;
}

/* Altta yatay dock (quicklaunch) */
/* From Plank's theme-Dark: fill 25,25,25 ~73%, line 100,100,100 ~40%,
 * icon 46, gap between icons 0.3 x icon = 14 pixels */
#quicklaunch, #quicklaunch:drop-target
{
	/* This fork draws the dock's background itself (so it can grow with the zoom);
	 * color, line and corners are read from the values below */
	background-type: none;
	background-fill-color: #191919bb;
	outline-color: #64646464;
	outline-width: 1.0;
	outline-borders: top left right bottom;
	corners: all;
	corner-radius: 16.0;
	margin-bottom: 6.0;
	spacing: 6.0;
}

/* This fork draws the running dots itself (like Plank: blue, glowing,
 * at the bottom edge of the dock) */
#quicklaunch XfdashboardApplicationButton.running
{
	effects: ;
}

/* "No Available Windows" text (macOS-like); the text is set below */
#empty-windows
{
	text: "@@EMPTYTEXT@@";
	button-style: text;
	font: "Inter Medium 13";
	color: #ffffffff;
	padding: 10.0;
	background-type: fill rounded-corners;
	background-fill-color: #00000055;
	corner-radius: 9.0;
}

/* App names in the dock (and other tooltips): rounded dark box like Plank.
 * This fork centers it above the dock icon. */
#tooltip
{
	background-type: fill rounded-corners;
	background-fill-color: #2b2b2deb;
	corner-radius: 7.0;
	outline-width: 0.0;
	padding: 6.0;
	font: "Inter Medium 12";
	text-color: #ffffffff;
}

XfdashboardWindowsView
{
	spacing: 22.0;
	prevent-upscaling: true;
	/* minimized windows are shown too, with their last picture before minimizing
	 * (this fork keeps it) */
	hide-minimized-windows: false;
}

/* Applications view as a grid instead of a list (like Launchpad) */
XfdashboardApplicationsView
{
	view-mode: icon;
	/* Like Launchpad: fixed 7 columns, plenty of space; the longest name does
	 * not decide the number of columns (this fork: columns) */
	columns: 7;
	spacing: 32.0;
	format-title-only: "%s";
}

/* Names on one line, ending in "…" if too long (like Launchpad) */
XfdashboardApplicationsView XfdashboardButton.view-mode-icon
{
	icon-size: 72.0;
	single-line: true;
	ellipsize-mode: end;
	padding: 10.0;
	spacing: 8.0;
}

/* App names in regular weight, not bold (like Launchpad); bold looked blurry at small sizes */
XfdashboardApplicationsView XfdashboardApplicationButton
{
	format-title-only: "%s";
}

/* With applications or search open, the wallpaper behind turns blurred and
 * darker like Launchpad (launchpad-background.png; this fork). No separate panel. */

/* No scrollbar (none on macOS either) */
XfdashboardScrollbar
{
	slider-color: #ffffff00;
	background-type: none;
	slider-width: 0.0;
}

/* No green "running" tick in applications/search */
XfdashboardApplicationsView XfdashboardApplicationButton.running,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardApplicationButton.running
{
	effects: ;
}

/* Search results like Spotlight: small icon, name, one faint line of description */
XfdashboardSearchResultContainer
{
	background-type: none;
}

XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardApplicationButton.result-item,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item.view-mode-list
{
	format-title-only: "%s";
	/* The CSS parser swallows quotes, so <small> instead of a span with attributes */
	format-title-description: "%s\n<small>%s</small>";
	icon-size: 36.0;
	single-line: false;
	ellipsize-mode: none;
	padding: 8.0;
	spacing: 12.0;
	font: "Inter Medium 12";
}

#quicklaunch XfdashboardButton
{
	icon-size: 46;
	padding: 4.0;
}

XfdashboardQuicklaunch
{
	normal-icon-size: 46.0;
	/* Zoom like Plank: icons grow above the dock, the dock keeps its height */
	hover-zoom: 1.32;
	/* Plank-like dots, apps button band, dividers, Launchpad position,
	 * folders/Trash at the end (this fork; off in other themes) */
	plank-style: true;
}

/* No permanent frame around windows (the theme's 2px grey box) */
XfdashboardLiveWindow
{
	background-type: none;
	outline-width: 0.0;
}

/* The window preview itself: rounded corners, no 1px black square line.
 * (corner-radius and the outline setting from the style need this fork) */
XfdashboardWindowContent
{
	/* radius at the real window size; this fork scales it down with the
	 * preview (like macOS) */
	corner-radius: 12.0;
	outline-width: 0.0;
}

/* Under the pointer / selected: thick blue rounded outline (like Mission Control) */
XfdashboardWindowsView XfdashboardLiveWindow:selected
{
	background-type: outline rounded-corners;
	outline-color: #3d8af7ff;
	outline-width: 3.0;
	/* the line wraps tightly around the preview; this fork derives its corner
	 * radius from the preview's current corner (inner edge = window corner,
	 * outer edge = that + line width) */
	outline-outset: 3.0;
	corner-radius: 9.0;
}

/* Title only under the pointer / when selected.
 * Hiding it with "visibility" crashes xfdashboard, so the title is painted
 * invisible instead: no background, fully transparent text, no icon.
 * Note: xfdashboard's CSS parser swallows the space in ":hover .title" and
 * reads it as ".title:hover"; that's why the rule below names the type. */
XfdashboardLiveWindow .title
{
	button-style: text;
	font: "Inter Medium 11";
	background-type: none;
	color: #ffffff00;
}

XfdashboardLiveWindow:selected XfdashboardButton.title
{
	background-type: fill rounded-corners;
	background-fill-color: #3a3a3cee;
	corner-radius: 7.0;
	padding: 6.0;
	font: "Inter Medium 12";
	color: #ffffffff;
}

/* Close button: no black square on every window, only a round grey cross
 * under the pointer (like macOS) */
XfdashboardLiveWindow XfdashboardButton.close-button
{
	icon-name: "@@DST@@/close-none.svg";
	background-type: none;
	button-style: icon;
}

XfdashboardLiveWindow:selected XfdashboardButton.close-button
{
	icon-name: "@@DST@@/close.svg";
	background-type: none;
}

/* Search box: completely invisible with no text (no background, icon or hint).
 * Appears as a pill as soon as you type in any view. */
#searchbox,
#searchbox.focus
{
	background-type: none;
	outline-width: 0.0;
	text-color: #ffffff00;
	hint-text-color: #ffffff00;
	selection-background-color: #ffffff00;
	primary-icon-name: "";
	secondary-icon-name: "";
	text-font: "Inter 13";
	padding: 8.0;
}

#searchbox.search-active,
#searchbox.search-active.focus
{
	background-type: fill rounded-corners;
	background-fill-color: #1e1e1ee6;
	corner-radius: 14.0;
	text-color: #ffffffff;
	selection-background-color: #3d8af7ff;
	primary-icon-name: "@@DST@@/search-light.svg";
	secondary-icon-name: "";
}

#searchbox XfdashboardButton.primary-icon
{
	sync-icon-size: true;
	padding: 1.0;
}

/* Applications: the same plain look under the pointer and when selected (one highlight only) */
XfdashboardApplicationsView XfdashboardButton,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton
{
	background-type: none;
	outline-width: 0.0;
	font: "Inter Medium 11";
}

/* One highlight: the selected item. The selection follows the pointer (this
 * fork) and the arrow keys; no separate hover color */
XfdashboardApplicationsView XfdashboardButton:hover,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton:hover,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item:hover
{
	background-type: none;
}

XfdashboardApplicationsView XfdashboardButton:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton:selected,
XfdashboardApplicationsView XfdashboardButton:hover:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton:hover:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item:hover:selected
{
	background-type: fill rounded-corners;
	background-fill-color: #ffffff22;
	outline-width: 0.0;
	corners: all;
	corner-radius: 14.0;
}

/* No background on hover in the dock; the icon grows like Plank */
#quicklaunch XfdashboardButton:hover
{
	background-type: none;
}

/* Apps button in the dock: no dark square while pressed, like the other icons */
#quicklaunch XfdashboardButton:selected,
XfdashboardQuicklaunch #applications-button:toggled,
#quicklaunch #applications-button:selected,
#quicklaunch #applications-button.search-active:selected,
#quicklaunch #applications-button:toggled:selected
{
	background-type: fill rounded-corners;
	background-fill-color: #ffffff22;
	outline-width: 0.0;
	corners: all;
	corner-radius: 12.0;
}

XfdashboardQuicklaunch #applications-button,
XfdashboardQuicklaunch #applications-button:toggled
{
	icon-name: "@@APPSICON@@";
}

/* ---- Plank look ---- */
#quicklaunch, #quicklaunch:drop-target { outline-width: 0.0; corner-radius: 24.0; }
XfdashboardQuicklaunch #applications-button:toggled,
#quicklaunch #applications-button:selected,
#quicklaunch #applications-button.search-active:selected,
#quicklaunch #applications-button:toggled:selected { background-type: none; }
EOF

# Running dot emblems (unused by the dock, which draws its own dots): light and dark
for c in light:f5f5f7 dark:1d1d1f; do
    n=${c%%:*}; col=${c#*:}
    cat > "$DST/running-dot-$n.svg" << SVG
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><circle cx="16" cy="29" r="2.4" fill="#$col"/></svg>
SVG
done
python3 - "$DST" << 'PY'
import sys, os
d=sys.argv[1]; p=os.path.join(d, "effects.xml")
s=open(p).read()
add=""
for n in ("light","dark"):
    add+=f"""
	<object id="running-dot-{n}" class="XfdashboardEmblemEffect">
		<property name="icon-name">{d}/running-dot-{n}.svg</property>
		<property name="icon-size">32</property>
		<property name="anchor-point">south</property>
		<property name="x-align">0.5</property>
		<property name="y-align">1.0</property>
	</object>
"""
s=s.replace("</effects>", add+"</effects>")
open(p,"w").write(s)
PY

# Animations: short, smooth (ease-out) transitions instead of blue's half-second
# linear ones; no half-second wait when switching views
python3 - "$DST" << 'PY'
import sys, os, re
p=os.path.join(sys.argv[1], "animations.xml")
s=open(p).read()
def trig(id_, body):
    global s
    s=re.sub(r'\s*<trigger id="%s".*?</trigger>' % id_, "", s, flags=re.S)
    s=s.replace("</animations>", body+"\n</animations>")
trig("application-startup", """
	<trigger id="application-startup" sender="XfdashboardStageInterface" signal="created">
		<timeline delay="0" duration="180" mode="ease-out-quad">
			<apply><property name="opacity" from="0" to="255" /></apply>
		</timeline>
	</trigger>""")
trig("view-visible", """
	<trigger id="view-visible" sender="XfdashboardView" signal="show">
		<timeline delay="0" duration="220" mode="ease-out-cubic">
			<apply><property name="opacity" from="0" to="255" /></apply>
		</timeline>
	</trigger>""")
trig("view-hidden", """
	<trigger id="view-hidden" sender="XfdashboardView" signal="hide">
		<timeline delay="0" duration="120" mode="ease-in-quad">
			<apply><property name="opacity" to="0" /></apply>
		</timeline>
	</trigger>""")
trig("window-moved", """
	<trigger id="window-moved" sender="XfdashboardLiveWindow" signal="move-resize">
		<timeline delay="0" duration="260" mode="ease-out-cubic">
			<apply>
				<property name="x" />
				<property name="y" />
				<property name="width" />
				<property name="height" />
			</apply>
		</timeline>
	</trigger>""")
trig("quicklaunch-item-moved", """
	<trigger id="quicklaunch-item-moved" sender="XfdashboardQuicklaunch XfdashboardApplicationButton" signal="move-resize">
		<timeline delay="0" duration="160" mode="ease-out-cubic">
			<apply>
				<property name="x" />
				<property name="y" />
				<property name="width" />
				<property name="height" />
			</apply>
		</timeline>
	</trigger>""")
trig("window-destroyed", """
	<trigger id="window-destroyed" sender="XfdashboardLiveWindowSimple" signal="destroy">
		<timeline delay="0" duration="160" mode="ease-in-quad">
			<apply><property name="opacity" to="0" /></apply>
		</timeline>
	</trigger>""")
open(p,"w").write(s)
PY

# Magnifier in the search box: light on the dark box, dark on the light box
for c in light:d0d0d4 dark:5a5a60; do
    n=${c%%:*}; col=${c#*:}
    cat > "$DST/search-$n.svg" << SVG
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><g fill="none" stroke="#$col" stroke-width="2.2" stroke-linecap="round"><circle cx="10" cy="10" r="6.5"/><path d="M15 15l5.5 5.5"/></g></svg>
SVG
done
cat > "$DST/close.svg" << 'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><circle cx="12" cy="12" r="10" fill="#4a4a4e" fill-opacity="0.92" stroke="#ffffff" stroke-opacity="0.25"/><path d="M8.5 8.5l7 7M15.5 8.5l-7 7" stroke="#f0f0f2" stroke-width="1.8" stroke-linecap="round"/></svg>
SVG
cat > "$DST/close-none.svg" << 'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"></svg>
SVG
sed -i "s|@@DST@@|$DST|g" "$DST/$STYLE"

# "No Available Windows" in the system language (Turkish or English)
case "${LC_ALL:-${LC_MESSAGES:-$LANG}}" in
    tr*) EMPTYTEXT="Kullanılabilir pencere yok" ;;
    *)   EMPTYTEXT="No Available Windows" ;;
esac
sed -i "s|@@EMPTYTEXT@@|$EMPTYTEXT|g" "$DST/$STYLE"

# Apps button: use a colored Launchpad icon from the icon theme if there is one
APPSICON=view-app-grid-symbolic
f=$(find "$HOME/.local/share/icons" "$HOME/.icons" /usr/share/icons /usr/local/share/icons \
        -path '*WhiteSur*' -path '*apps*' -iname 'launchpad.*' 2>/dev/null | head -1)
[ -n "$f" ] && APPSICON=$(basename "${f%.*}")
# Your own Launchpad icon wins if present
[ -f "$HOME/.local/share/icons/launchpad.svg" ] && APPSICON="$HOME/.local/share/icons/launchpad.svg"
sed -i "s|@@APPSICON@@|$APPSICON|g" "$DST/$STYLE"
echo "Applications button icon: $APPSICON"

# Dark theme (xfdashboard-mac-dark): same theme, white text
DARK="$HOME/.themes/xfdashboard-mac-dark/xfdashboard-1.0"
rm -rf "$DARK"
mkdir -p "$DARK"
cp -r "$DST"/. "$DARK"/
sed -i 's/^Name=.*/Name=xfdashboard-mac-dark/;
        s/^Comment=.*/Comment=macOS-like Mission Control with a Plank-like dock (dark)/;
        s/^Screenshot=.*/Screenshot=screenshot-xfdashboard-mac-dark.png/' "$DARK/xfdashboard.theme"
rm -f "$DARK"/screenshot-xfdashboard-mac.png
[ -f "$HERE/screenshot-xfdashboard-mac-dark.png" ] && cp "$HERE/screenshot-xfdashboard-mac-dark.png" "$DARK/"
cat >> "$DARK/$STYLE" << 'EOF'

/* ---- dark colors ---- */
XfdashboardLiveWorkspace XfdashboardButton.title { color: #ffffffff; }
#add-workspace { color: #ffffffc0; }
#add-workspace:hover { color: #ffffffff; background-fill-color: #ffffff30; }
XfdashboardLiveWindow:selected XfdashboardButton.title { background-fill-color: #191919bb; }
#empty-windows { background-fill-color: #191919bb; }

EOF

# Light theme (xfdashboard-mac): light strip, dark text
cat >> "$DST/$STYLE" << 'EOF'

/* ---- light colors ---- */
XfdashboardLiveWorkspace XfdashboardButton.title { color: #1d1d1fff; }
#add-workspace { color: #1d1d1fc0; }
#add-workspace:hover { color: #1d1d1fff; background-fill-color: #00000018; }

#quicklaunch, #quicklaunch:drop-target { background-fill-color: #ffffff5a; outline-color: #ffffff38; }
#quicklaunch XfdashboardButton:selected,
XfdashboardQuicklaunch #applications-button:toggled,
#quicklaunch #applications-button:selected,
#quicklaunch #applications-button:toggled:selected { background-fill-color: #00000018; }
XfdashboardApplicationsView XfdashboardButton:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item:selected,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item:hover:selected { background-fill-color: #ffffff40; }
#empty-windows { background-fill-color: #e8e8eaee; color: #1d1d1fff; }

XfdashboardLiveWindow:selected XfdashboardButton.title { background-fill-color: #e8e8e880; color: #1d1d1fff; }
#quicklaunch, #quicklaunch:drop-target { background-fill-color: #e8e8e880; }
#empty-windows { background-fill-color: #e8e8e880; }

#searchbox.search-active,
#searchbox.search-active.focus { background-fill-color: #f4f4f6f0; text-color: #1d1d1fff; primary-icon-name: "@@DST@@/search-dark.svg"; }
EOF
sed -i "s|@@DST@@|$DST|g" "$DST/$STYLE"

# Clicking a running app in the dock switches to its window instead of opening a new one (like macOS/Plank)
xfconf-query -c xfdashboard -p /always-launch-new-instance -n -t bool -s false

# Pick the right one for the current GTK theme now (an automatic day/night switcher can take over later)
if xfconf-query -c xsettings -p /Net/ThemeName 2>/dev/null | grep -qi dark; then
    xfconf-query -c xfdashboard -p /theme -n -t string -s xfdashboard-mac-dark
else
    xfconf-query -c xfdashboard -p /theme -n -t string -s xfdashboard-mac
fi

echo "Installed: $DST and $DARK"
echo "Light: xfdashboard-mac, dark: xfdashboard-mac-dark."
echo "To try one: xfconf-query -c xfdashboard -p /theme -s xfdashboard-mac-dark; xfdashboard -r"
