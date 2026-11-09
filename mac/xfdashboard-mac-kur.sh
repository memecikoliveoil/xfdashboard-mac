#!/bin/sh
# xfdashboard-blue temasını temel alıp Mission Control'e benzeyen "xfdashboard-mac" teması kurar.
# - Çalışma alanları sağdan üste, yatay şerit olarak taşınır, isimleri görünür
# - Ekran karartması büyük ölçüde azaltılır (duvar kağıdı görünür kalır)
# - Quicklaunch (uygulama çubuğu) soldan alta, yatay dock olarak taşınır
# - Pencere başlığı sadece fare üstündeyken görünür
# Sistem temasına dokunmaz; her şey ~/.themes/xfdashboard-mac altına yazılır.
set -e

# Önizleme resimleri bu betiğin yanında duruyor
# (screenshot-xfdashboard-mac.png, screenshot-xfdashboard-mac-dark.png)
HERE=$(cd "$(dirname "$0")" && pwd)

SRC=""
for d in /usr/local/share/themes/xfdashboard-blue/xfdashboard-1.0 \
         /usr/share/themes/xfdashboard-blue/xfdashboard-1.0 \
         "$HOME/.themes/xfdashboard-blue/xfdashboard-1.0" \
         "$HOME/.local/share/themes/xfdashboard-blue/xfdashboard-1.0"; do
    [ -d "$d" ] && SRC="$d" && break
done
[ -n "$SRC" ] || { echo "xfdashboard-blue teması bulunamadı" >&2; exit 1; }

DST="$HOME/.themes/xfdashboard-mac/xfdashboard-1.0"
rm -rf "$DST"
mkdir -p "$DST"
cp -r "$SRC"/. "$DST"/

# Tema adı
# (Name[tr]= gibi çevrilmiş satırlar silinmezse ayarlarda yine "xfdashboard-blue" görünür)
sed -i '/^Name\[/d; /^Comment\[/d;
        s/^Name=.*/Name=xfdashboard-mac/;
        s/^Comment=.*/Comment=macOS-like Mission Control with a Plank-like dock (light)/;
        s/^Author=.*/Author=memecikoliveoil/;
        s/^Screenshot=.*/Screenshot=screenshot-xfdashboard-mac.png/' \
    "$DST/xfdashboard.theme"
rm -f "$DST"/screenshot-xfdashboard-blue.png
[ -f "$HERE/screenshot-xfdashboard-mac.png" ] && cp "$HERE/screenshot-xfdashboard-mac.png" "$DST/"

# Yerleşim: üstte çalışma alanı şeridi, altında arama + görünüm seçici, en altta pencereler
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

				<!-- Spaces şeridi. Arkasındaki bulanık + hafif açık bant duvar kağıdına
				     auto_theme.py tarafından işleniyor (BAND=164 piksel). -->
				<child>
					<object class="XfdashboardWorkspaceSelector" id="workspace-selector">
						<property name="can-focus">true</property>
						<property name="x-expand">false</property>
						<property name="x-align">center</property>
						<property name="orientation">horizontal</property>
						<property name="show-current-monitor-only">true</property>
					</object>
				</child>

				<!-- Görünüm seçici yok; arama kutusu aşağıda, şeridin altında yüzen görünmez kutu. -->

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

		<!-- Sağ üstteki "+" : yeni masaüstü ekler (yamalı xfdashboard'daki add-workspace eylemi) -->
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

		<!-- Görünmez arama: yazmaya başlayınca şeridin altında belirir (yazı yokken hiç görünmez) -->
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

		<!-- Hiç pencere yokken ortada çıkan yazı (Mac'teki "Kullanılabilir pencere yok") -->
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

# Renk/görünüm: blue temanın stil dosyasının sonuna ekle (sonraki kurallar öncekileri ezer)
STYLE=$(sed -n 's/^Style=//p' "$DST/xfdashboard.theme" | cut -d';' -f1)
cat >> "$DST/$STYLE" << 'EOF'

/* ---- xfdashboard-mac ---- */

/* Karartma: blue temada #000000d0 (yaklaşık %80 siyah). Hafif bir gölge bırak. */
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

/* Sağ üstteki "+" : yeni masaüstü ekler */
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

/* Masaüstü küçük resimleri: çerçevesiz, adı küçük resmin ALTINDA (yamalı
 * xfdashboard'daki workspace-name-below), etkin olanın etrafında mavi çerçeve */
XfdashboardLiveWorkspace
{
	show-workspace-name: true;
	workspace-name-below: true;
	workspace-name-padding: 5.0;
	background-type: none;
	outline-width: 0.0;
}

/* Etkin masaüstü: küçük resim köşeli olduğu için çerçeve de köşeli, boşluksuz
 * tam dışına sarılır (çizgi kalınlığı = dışa taşma) */
XfdashboardLiveWorkspace:active
{
	background-type: outline;
	outline-color: #3d8af7ff;
	outline-width: 3.0;
	outline-outset: 3.0;
}

/* Masaüstü adı: zeminsiz düz yazı. Rengi gündüz/gece temasında ayrı (dosyanın sonu). */
XfdashboardLiveWorkspace XfdashboardButton.title
{
	button-style: text;
	background-type: none;
	font: "Inter Medium 10";
	color: #1d1d1fff;
}

/* Arama kutusu ve görünüm seçici fazla göze batmasın */
#searchbox, #view-selector
{
	background-fill-color: #00000040;
}

/* Altta yatay dock (quicklaunch) */
/* Plank temasından (theme-Dark): zemin 25,25,25 ~%73, çizgi 100,100,100 ~%40,
 * ikon 46, ikonlar arası 0.3 x ikon ≈ 14 piksel */
#quicklaunch, #quicklaunch:drop-target
{
	/* Zemini yamalı xfdashboard kendisi çiziyor (zoom'la birlikte büyüyebilsin diye);
	 * renk, çizgi ve köşe aşağıdaki değerlerden okunuyor */
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

/* Docktaki "çalışıyor" noktalarını yamalı xfdashboard kendisi çiziyor (Plank gibi
 * mavi, parlayan, pencere sayısı kadar, dock'un alt kenarına yakın) */
#quicklaunch XfdashboardApplicationButton.running
{
	effects: ;
}

/* Pencereler boş alanı daha iyi doldursun (Mission Control gibi büyük önizlemeler) */
#empty-windows
{
	text: "Kullanılabilir pencere yok";
	button-style: text;
	font: "Inter Medium 13";
	color: #ffffffff;
	padding: 10.0;
	background-type: fill rounded-corners;
	background-fill-color: #00000055;
	corner-radius: 9.0;
}

/* Docktaki uygulama adı (ve diğer ipuçları): Plank gibi yuvarlak köşeli koyu kutu.
 * Dock simgelerinde yamalı xfdashboard bunu simgenin üstüne ortalar. */
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
	/* simge durumundaki pencereler de görünür: küçültülmeden önceki son hali
	 * (yamalı xfdashboard görüntüsünü saklıyor) */
	hide-minimized-windows: false;
}

/* Uygulamalar görünümü liste yerine ızgara (Launchpad gibi) */
XfdashboardApplicationsView
{
	view-mode: icon;
	/* Launchpad gibi sabit 7 sütun, aralarda bolca boşluk; en uzun ad sütun
	 * sayısını belirlemesin (yamalı xfdashboard: columns) */
	columns: 7;
	spacing: 32.0;
	format-title-only: "%s";
}

/* Adlar tek satır, sığmazsa sonu "…" (Launchpad gibi) */
XfdashboardApplicationsView XfdashboardButton.view-mode-icon
{
	icon-size: 72.0;
	single-line: true;
	ellipsize-mode: end;
	padding: 10.0;
	spacing: 8.0;
}

/* Uygulama adları kalın değil, normal (Launchpad gibi); kalın yazı küçük boyda bulanık duruyordu */
XfdashboardApplicationsView XfdashboardApplicationButton
{
	format-title-only: "%s";
}

/* Uygulamalar ve arama açıkken arkadaki duvar kağıdı Launchpad gibi bulanık ve
 * koyulaşmış haline geçiyor (launchpad-background.png, auto_theme.py üretiyor;
 * yamalı xfdashboard). Ayrı bir panel yok. */

/* Kaydırma çubuğu görünmesin (Mac'te de yok) */
XfdashboardScrollbar
{
	slider-color: #ffffff00;
	background-type: none;
	slider-width: 0.0;
}

/* Uygulamalar/aramada yeşil "çalışıyor" tiki yok */
XfdashboardApplicationsView XfdashboardApplicationButton.running,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardApplicationButton.running
{
	effects: ;
}

/* Arama sonuçları Spotlight gibi: küçük ikon, ad ve altında soluk tek satır açıklama */
XfdashboardSearchResultContainer
{
	background-type: none;
}

XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardApplicationButton.result-item,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton.result-item.view-mode-list
{
	format-title-only: "%s";
	/* CSS okuyucusu tırnakları yuttuğu için öznitelikli span yerine <small> */
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
	/* Büyütme Plank gibi: simge dock'un üstüne taşar, dock'un boyu değişmez */
	hover-zoom: 1.32;
	/* Plank gibi noktalar, uygulamalar düğmesinin bandı, ayraçlar, Launchpad
	 * yeri, klasör/Çöp sağda (yamalı xfdashboard; diğer temalarda kapalı) */
	plank-style: true;
}

/* Pencerelerin etrafında sürekli çerçeve olmasın (temanın 2px gri kutusu) */
XfdashboardLiveWindow
{
	background-type: none;
	outline-width: 0.0;
}

/* Pencere önizlemesinin kendisi: köşeler yuvarlak, 1px siyah kare çizgi yok.
 * (corner-radius ve stilden outline ayarı yamalı xfdashboard ister) */
XfdashboardWindowContent
{
	/* gerçek pencere boyutundaki yarıçap; önizleme küçüldükçe yamalı xfdashboard
	 * bunu da aynı oranda küçültüyor (Mac'teki gibi) */
	corner-radius: 12.0;
	outline-width: 0.0;
}

/* Fare üstündeyken / seçiliyken: mavi, yuvarlak köşeli kalın çerçeve (Mission Control gibi) */
XfdashboardWindowsView XfdashboardLiveWindow:selected
{
	background-type: outline rounded-corners;
	outline-color: #3d8af7ff;
	outline-width: 3.0;
	/* çizgi önizlemenin tam dışına, boşluksuz sarılır; köşe yuvarlaklığını
	 * yamalı xfdashboard önizlemenin o anki köşesinden hesaplar (iç kenar =
	 * pencere köşesi, dış kenar = o + çizgi kalınlığı) */
	outline-outset: 3.0;
	corner-radius: 9.0;
}

/* Başlık sadece fare üstündeyken / seçiliyken.
 * "visibility" ile gizlemek xfdashboard'u çökertiyor, o yüzden başlığı
 * görünmez renklere boyuyoruz: zemin yok, yazı tamamen saydam, ikon yok.
 * Not: xfdashboard'un CSS okuyucusu ":hover .title" yazımında boşluğu yutup
 * bunu ".title:hover" sanıyor; o yüzden alttaki kuralda tip adı açıkça yazılı. */
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

/* Kapatma düğmesi: her pencerede siyah kare yerine, sadece fare üstündeyken
 * yuvarlak gri bir çarpı (Mac'teki gibi) */
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

/* Arama kutusu: yazı yokken tamamen görünmez (zemin, ikon, ipucu yok).
 * Herhangi bir görünümde yazmaya başlayınca hap şeklinde belirir. */
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

/* Uygulamalar: üstüne gelince ve seçiliyken aynı sade görünüm (iki farklı vurgu olmasın) */
XfdashboardApplicationsView XfdashboardButton,
XfdashboardSearchView XfdashboardSearchResultContainer XfdashboardButton
{
	background-type: none;
	outline-width: 0.0;
	font: "Inter Medium 11";
}

/* Tek vurgu: seçili öğe. Fare üstüne gelince seçim oraya geçiyor (yamalı
 * xfdashboard), oklarla gezince de seçim hareket ediyor; ayrı bir hover rengi yok */
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

/* Dock'ta üstüne gelince zemin yok, Plank gibi ikon büyüyor (animations.xml) */
#quicklaunch XfdashboardButton:hover
{
	background-type: none;
}

/* Docktaki uygulamalar düğmesi: basılıyken koyu kare yok, diğer ikonlar gibi */
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

/* ---- Plank'e benzetme ---- */
#quicklaunch, #quicklaunch:drop-target { outline-width: 0.0; corner-radius: 24.0; }
XfdashboardQuicklaunch #applications-button:toggled,
#quicklaunch #applications-button:selected,
#quicklaunch #applications-button.search-active:selected,
#quicklaunch #applications-button:toggled:selected { background-type: none; }
EOF

# Docktaki "çalışıyor" noktası: iki renk (gece açık, gündüz koyu)
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

# Animasyonlar: blue temadaki yarım saniyelik doğrusal geçişler yerine kısa ve
# yumuşak (ease-out) geçişler; görünüm değişirken yarım saniyelik bekleme yok
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

# Arama kutusundaki büyüteç: koyu kutuda açık, açık kutuda koyu
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

# Uygulamalar düğmesi: simge temasında renkli bir Launchpad simgesi varsa onu kullan
APPSICON=view-app-grid-symbolic
f=$(find "$HOME/.local/share/icons" "$HOME/.icons" /usr/share/icons /usr/local/share/icons \
        -path '*WhiteSur*' -path '*apps*' -iname 'launchpad.*' 2>/dev/null | head -1)
[ -n "$f" ] && APPSICON=$(basename "${f%.*}")
# Kendi Launchpad simgen varsa o kullanılır
[ -f "$HOME/.local/share/icons/launchpad.svg" ] && APPSICON="$HOME/.local/share/icons/launchpad.svg"
sed -i "s|@@APPSICON@@|$APPSICON|g" "$DST/$STYLE"
echo "Uygulamalar düğmesi simgesi: $APPSICON"

# Gece teması (xfdashboard-mac-dark): aynı tema, yazılar beyaz
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

/* ---- gece renkleri ---- */
XfdashboardLiveWorkspace XfdashboardButton.title { color: #ffffffff; }
#add-workspace { color: #ffffffc0; }
#add-workspace:hover { color: #ffffffff; background-fill-color: #ffffff30; }
XfdashboardLiveWindow:selected XfdashboardButton.title { background-fill-color: #191919bb; }
#empty-windows { background-fill-color: #191919bb; }

EOF

# Gündüz teması (xfdashboard-mac): açık şerit, koyu yazılar
cat >> "$DST/$STYLE" << 'EOF'

/* ---- gündüz renkleri ---- */
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

# Dock'ta açık bir uygulamaya tıklayınca yenisini açmak yerine açık pencereye geç (Mac/Plank gibi)
xfconf-query -c xfdashboard -p /always-launch-new-instance -n -t bool -s false

# Şu anki GTK temasına göre hemen doğru olanı seç (sonrasını auto_theme.py yapıyor)
if xfconf-query -c xsettings -p /Net/ThemeName 2>/dev/null | grep -qi dark; then
    xfconf-query -c xfdashboard -p /theme -n -t string -s xfdashboard-mac-dark
else
    xfconf-query -c xfdashboard -p /theme -n -t string -s xfdashboard-mac
fi

echo "Kuruldu: $DST ve $DARK"
echo "auto_theme.py gündüz xfdashboard-mac, gece xfdashboard-mac-dark seçiyor."
echo "Elle denemek için: xfconf-query -c xfdashboard -p /theme -s xfdashboard-mac-dark; xfdashboard -r"
