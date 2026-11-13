"""Builds docs/WebbyLens-User-Guide.pdf (needs reportlab and pillow).

    python docs/make_guide.py
"""
import re
from datetime import date
from pathlib import Path

from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.platypus import (Image as RLImage, KeepTogether, PageBreak, Paragraph,
                                SimpleDocTemplate, Spacer, Table, TableStyle)

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
TMP = ROOT / "build" / "guide"
OUT = ROOT / "docs" / "WebbyLens-User-Guide.pdf"

ACCENT = colors.Color(28 / 255, 70 / 255, 170 / 255)
LIGHT = colors.Color(232 / 255, 240 / 255, 252 / 255)
TIP_BG = colors.Color(240 / 255, 248 / 255, 236 / 255)
WARN_BG = colors.Color(253 / 255, 243 / 255, 228 / 255)
GRID = colors.Color(0.80, 0.84, 0.90)


def version() -> str:
    h = (ROOT / "src" / "common" / "version.h").read_text()
    parts = [re.search(rf"VERSION_{k} (\d+)", h).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


VERSION = version()

# ---------------------------------------------------------------- styles
ss = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=ss["Normal"], fontName="Helvetica", fontSize=10.5, leading=15,
                      spaceAfter=6)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=9, leading=12, spaceAfter=0)
CELL = ParagraphStyle("cell", parent=BODY, fontSize=9.5, leading=13, spaceAfter=0)
CELLB = ParagraphStyle("cellb", parent=CELL, fontName="Helvetica-Bold")
H1 = ParagraphStyle("h1", parent=BODY, fontName="Helvetica-Bold", fontSize=17, leading=22,
                    textColor=ACCENT, spaceBefore=6, spaceAfter=10)
H2 = ParagraphStyle("h2", parent=BODY, fontName="Helvetica-Bold", fontSize=12.5, leading=17,
                    textColor=ACCENT, spaceBefore=10, spaceAfter=5)
BULLET = ParagraphStyle("bullet", parent=BODY, leftIndent=14, bulletIndent=3, spaceAfter=3)
STEP = ParagraphStyle("step", parent=BODY, leftIndent=18, bulletIndent=0, spaceAfter=4)
TITLE = ParagraphStyle("title", parent=BODY, fontName="Helvetica-Bold", fontSize=30, leading=36,
                       alignment=TA_CENTER, textColor=ACCENT)
SUBTITLE = ParagraphStyle("subtitle", parent=BODY, fontSize=14, leading=19, alignment=TA_CENTER,
                          textColor=colors.Color(0.25, 0.28, 0.35))
CENTER = ParagraphStyle("center", parent=BODY, alignment=TA_CENTER)

story = []


def p(text, style=BODY):
    story.append(Paragraph(text, style))


def h1(text):
    story.append(Paragraph(text, H1))


def h2(text):
    story.append(Paragraph(text, H2))


def bullets(items):
    for it in items:
        story.append(Paragraph(it, BULLET, bulletText="\u2022"))


def steps(items):
    for i, it in enumerate(items, 1):
        story.append(Paragraph(it, STEP, bulletText=f"{i}."))


def box(text, kind="tip"):
    bg, label = (TIP_BG, "Tip") if kind == "tip" else (WARN_BG, "Important")
    t = Table([[Paragraph(f"<b>{label}:</b> {text}", CELL)]], colWidths=[16.4 * cm])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), bg),
        ("BOX", (0, 0), (-1, -1), 0.5, GRID),
        ("LEFTPADDING", (0, 0), (-1, -1), 9), ("RIGHTPADDING", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
    ]))
    story.append(Spacer(1, 4))
    story.append(t)
    story.append(Spacer(1, 8))


def table(rows, widths, header=True):
    data = []
    for r, row in enumerate(rows):
        data.append([c if not isinstance(c, str) else
                     Paragraph(c, CELLB if (header and r == 0) else CELL) for c in row])
    t = Table(data, colWidths=[w * cm for w in widths], repeatRows=1 if header else 0)
    style = [
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.4, GRID),
        ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]
    if header:
        style.append(("BACKGROUND", (0, 0), (-1, 0), LIGHT))
    t.setStyle(TableStyle(style))
    story.append(t)
    story.append(Spacer(1, 8))


def icon_png(ico: str, size: int) -> str:
    """Extracts one size of an .ico file as PNG for embedding."""
    TMP.mkdir(parents=True, exist_ok=True)
    im = Image.open(ASSETS / ico)
    im.size = (size, size)
    path = TMP / f"{Path(ico).stem}-{size}.png"
    im.convert("RGBA").save(path)
    return str(path)


def icon(ico: str, cm_size: float = 0.8):
    return RLImage(icon_png(ico, 48), width=cm_size * cm, height=cm_size * cm)


def flow_diagram():
    """Phone -> WebbyLens -> apps, as a row of coloured boxes."""
    def cell(title, text):
        return Paragraph(f"<b>{title}</b><br/>{text}", ParagraphStyle("fd", parent=CELL, alignment=TA_CENTER))
    arrow = Paragraph("<b>&gt;</b>", ParagraphStyle("ar", parent=CELL, alignment=TA_CENTER, fontSize=16,
                                                    textColor=ACCENT, leading=20))
    t = Table([[cell("Android phone", "camera + microphone"), arrow,
                cell("USB cable", "no app on the phone"), arrow,
                cell("WebbyLens", "runs in the system tray"), arrow,
                cell("Teams, Zoom, ...", "camera <i>WebbyLens</i><br/>mic <i>CABLE Output</i>")]],
              colWidths=[3.6 * cm, 0.7 * cm, 3.3 * cm, 0.7 * cm, 3.5 * cm, 0.7 * cm, 3.9 * cm])
    t.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        *[("BACKGROUND", (c, 0), (c, 0), LIGHT) for c in (0, 2, 4, 6)],
        *[("BOX", (c, 0), (c, 0), 0.6, ACCENT) for c in (0, 2, 4, 6)],
        ("TOPPADDING", (0, 0), (-1, -1), 8), ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
    ]))
    story.append(t)
    story.append(Spacer(1, 10))


# ---------------------------------------------------------------- content
# Cover
story.append(Spacer(1, 3.2 * cm))
story.append(RLImage(str(ASSETS / "webbylens-512.png"), width=5 * cm, height=5 * cm))
story.append(Spacer(1, 1 * cm))
story.append(Paragraph("WebbyLens", TITLE))
story.append(Spacer(1, 0.4 * cm))
story.append(Paragraph("User Guide", SUBTITLE))
story.append(Spacer(1, 1.2 * cm))
story.append(Paragraph("Use your Android phone as a webcam and microphone<br/>on Windows 11 - over USB, "
                       "with no app on the phone", SUBTITLE))
story.append(Spacer(1, 3.5 * cm))
story.append(Paragraph(f"Version {VERSION} &nbsp;&nbsp;|&nbsp;&nbsp; {date.today():%B %Y}", CENTER))
story.append(PageBreak())

# Contents
h1("Contents")
contents = [
    "1. What WebbyLens does", "2. What you need", "3. Prepare the phone (USB debugging)",
    "4. Install VB-Audio Virtual Cable (for the microphone)", "5. Install WebbyLens",
    "6. First start - the tray icon", "7. Use WebbyLens in Teams, Zoom and other programs",
    "8. The four modes", "9. The menu", "10. The floating window", "11. Camera, lenses and zoom",
    "12. Pause and automatic standby", "13. Settings window and start with Windows",
    "14. Troubleshooting", "15. Privacy", "16. Uninstall", "17. License and credits",
]
for c in contents:
    p(c, ParagraphStyle("toc", parent=BODY, spaceAfter=2))
story.append(PageBreak())

# 1
h1("1. What WebbyLens does")
p("WebbyLens turns an Android phone into a webcam <b>and</b> a microphone for a Windows 11 PC. "
  "The phone is connected with a USB cable; nothing has to be installed on the phone. On the PC, "
  "programs such as Microsoft Teams, Zoom, Google Meet in a browser or OBS see:")
bullets(["a camera called <b>WebbyLens</b> (your phone's front or back camera, 1920 x 1080 or 1280 x 720, 30 frames per second),",
         "a microphone called <b>CABLE Output</b> (your phone's microphone, through the free VB-Audio Virtual Cable)."])
p("WebbyLens can also show the camera picture in a small <b>floating window</b> that stays on top of "
  "other windows, for example as a mirror or for presentations.")
flow_diagram()
p("WebbyLens works quietly in the background. It has no main window: it lives as an icon in the "
  "<b>system tray</b> (the notification area next to the clock), and everything is controlled from "
  "that icon's menu.")

# 2
h1("2. What you need")
table([
    ["Item", "Details"],
    ["PC", "Windows 11 (the virtual camera uses a Windows 11 feature; Windows 10 is not supported)."],
    ["Phone", "Android 12 or newer, with <b>USB debugging</b> switched on (chapter 3)."],
    ["Cable", "A USB <b>data</b> cable. Some cheap cables only charge - if the phone is never found, try another cable."],
    ["Microphone", "<b>VB-Audio Virtual Cable</b>, free, installed once (chapter 4). Without it, the camera works but the "
                   "phone microphone does not."],
    ["Internet", "Only during installation, to download Google's adb tool. WebbyLens itself does not use the internet."],
], [3.2, 13.2])

# 3
h1("3. Prepare the phone (USB debugging)")
p("WebbyLens talks to the phone through <i>adb</i>, Android's official tool for USB connections. "
  "For this, USB debugging must be switched on once:")
steps([
    "Open <b>Settings - About phone</b>.",
    "Tap <b>Build number</b> seven times (on Samsung: <i>Software information - Build number</i>; on Xiaomi: "
    "<i>MIUI/OS version</i>). Enter your PIN if asked. The message \"You are now a developer\" appears.",
    "Go back to Settings and open <b>System - Developer options</b> (on some phones directly in Settings).",
    "Switch on <b>USB debugging</b>.",
    "Connect the phone to the PC with the USB cable and unlock the phone.",
    "A dialog <b>\"Allow USB debugging?\"</b> appears on the phone. Tick <b>\"Always allow from this computer\"</b> "
    "and tap <b>Allow</b>.",
])
box("If the dialog does not appear, unplug and plug in the cable again with the phone unlocked. If the "
    "phone asks what the USB connection is for, any option works (\"File transfer\" is fine).")
box("USB debugging allows a connected computer to control the phone. Only allow computers you trust, "
    "and switch USB debugging off again if you stop using WebbyLens.", "warn")

# 4
h1("4. Install VB-Audio Virtual Cable (for the microphone)")
p("Windows does not let ordinary programs create a microphone. The free <b>VB-Audio Virtual Cable</b> "
  "provides one: WebbyLens plays the phone's sound into the cable, and Teams or Zoom record it from the "
  "other end.")
steps([
    "Open <b>https://vb-audio.com/Cable/</b> and download the Windows package (a ZIP file, e.g. "
    "<i>VBCABLE_Driver_Pack46.zip</i>).",
    "Extract the ZIP file (right-click - <i>Extract All</i>).",
    "Right-click <b>VBCABLE_Setup_x64.exe</b> and choose <b>Run as administrator</b>, then click <b>Install Driver</b>.",
    "<b>Restart Windows.</b>",
])
p("Afterwards Windows has two new sound devices:")
table([
    ["Device", "Kind", "Used by"],
    ["CABLE Input", "playback (speaker)", "WebbyLens plays the phone microphone into it - you never select it yourself"],
    ["CABLE Output", "recording (microphone)", "select this as the microphone in Teams, Zoom, ..."],
], [3.4, 4.0, 9.0])
box("Do not make \"CABLE Input\" your default speaker - you would hear nothing. WebbyLens uses it "
    "directly. You may rename \"CABLE Output\" (Settings - System - Sound - CABLE Output - Properties), "
    "for example to \"Phone microphone\".")

# 5
h1("5. Install WebbyLens")
steps([
    "Download <b>WebbyLens-Setup-%s.exe</b> from <b>https://github.com/crunchy3d/WebbyLens/releases</b> and run it." % VERSION,
    "Windows may show <b>\"Windows protected your PC\"</b> because the installer is not code-signed. "
    "Click <b>More info - Run anyway</b>.",
    "Confirm the administrator prompt. Administrator rights are needed once, to register the virtual camera.",
    "Accept the license (MIT) and the installation folder (default <i>C:\\Program Files\\WebbyLens</i>).",
    "Choose whether WebbyLens should <b>start automatically when you sign in</b> (recommended) and whether "
    "you want a desktop shortcut.",
    "The installer downloads <b>Android SDK Platform-Tools (adb)</b> from Google. It is provided by Google "
    "under its own terms and is needed to talk to the phone.",
    "On the last page, WebbyLens can be started right away. If VB-Audio Virtual Cable is not installed yet, "
    "the installer offers to open its download page.",
])
box("After installation <b>WebbyLens works in the system tray</b> - no window opens. Look for its icon in "
    "the notification area next to the clock (chapter 6).", "warn")
p("The Start menu contains <b>WebbyLens</b> (to start it) and this <b>User Guide</b>.")

# 6
h1("6. First start - the tray icon")
p("When WebbyLens runs, its icon is in the <b>system tray</b>, the notification area at the right end of "
  "the taskbar, next to the clock. Windows 11 often hides new icons: click the small <b>^</b> arrow to "
  "see hidden icons. To keep the WebbyLens icon always visible, drag it from there onto the taskbar, or "
  "switch it on in <i>Settings - Personalization - Taskbar - Other system tray icons</i>.")
p("The icon always has the same shape - a lens sending a signal - and its <b>colour</b> shows what "
  "WebbyLens is doing. Hover over it to see a short status text.")
table([
    ["Icon", "Meaning"],
    [icon("tray-live.ico"), "<b>Blue - live:</b> the phone camera is streaming."],
    [icon("tray-sound.ico"), "<b>Green - sound only:</b> only the phone microphone is used, the phone camera is off."],
    [icon("tray-standby.ico"), "<b>Grey - standby or waiting:</b> no program uses the camera or microphone right now, "
                              "or the phone is not connected yet."],
    [icon("tray-paused.ico"), "<b>Orange - paused:</b> you paused WebbyLens with Ctrl+Alt+P (chapter 12)."],
], [2.0, 14.4])
p("<b>Click the icon</b> (left or right mouse button) to open the menu. Then connect the phone with the "
  "USB cable - WebbyLens finds it automatically and reconnects by itself if the cable is unplugged and "
  "plugged in again.")

# 7
h1("7. Use WebbyLens in Teams, Zoom and other programs")
p("Select WebbyLens once in each program; the programs remember it.")
table([
    ["Program", "Where to select camera and microphone"],
    ["Microsoft Teams", "<i>Settings (...) - Settings - Devices</i>: Camera <b>WebbyLens</b>, Microphone <b>CABLE Output</b>. "
                        "In a meeting also via <i>More - Settings - Device settings</i>."],
    ["Zoom", "<i>Settings - Video</i>: Camera <b>WebbyLens</b>. <i>Settings - Audio</i>: Microphone <b>CABLE Output</b>."],
    ["Browser (Google Meet, Jitsi, ...)", "The browser asks for camera/microphone permission; choose <b>WebbyLens</b> and "
                                          "<b>CABLE Output</b> in the site settings (camera icon in the address bar)."],
    ["Windows Camera app, OBS", "Choose the camera <b>WebbyLens</b>."],
], [4.4, 12.0])
p("WebbyLens offers the picture in 1920 x 1080 and 1280 x 720; the program chooses. Your speakers and "
  "headphones stay as they are - the phone sound never plays through them.")
box("With automatic standby (on by default) the phone starts streaming about 2-4 seconds after a program "
    "opens the camera or microphone. During these seconds the camera shows a dark picture with a "
    "crossed-out phone - this is normal.")

# 8
h1("8. The four modes")
p("Choose the mode in the menu at any time, also during a call. Switching takes 1-2 seconds.")
table([
    ["Mode", "WebbyLens camera in Teams/Zoom", "Floating window", "Phone microphone"],
    ["Webcam", "phone picture", "-", "yes"],
    ["Webcam + floating window", "phone picture", "yes", "yes"],
    ["Floating window only", "\"no signal\" picture", "yes", "yes"],
    ["Sound only (microphone)", "\"no signal\" picture", "-", "yes (phone camera off)"],
], [4.6, 4.8, 3.2, 3.8])
p("\"Floating window only\" is useful when you want to see yourself without sending the picture anywhere. "
  "\"Sound only\" uses the phone purely as a microphone and saves the phone's battery.")

# 9
h1("9. The menu")
p("Click the tray icon, or right-click the floating window. The menu contains:")
table([
    ["Item", "What it does"],
    ["Status lines", "Version, current state, and warnings (e.g. VB-Cable missing)."],
    ["Pause / Resume", "Pauses everything (chapter 12). Shortcut <b>Ctrl+Alt+P</b>."],
    ["Stream only when used", "Automatic standby on/off (chapter 12). On by default."],
    ["Settings...", "Opens the settings window (chapter 13)."],
    ["Exit full screen / Close floating window", "Shown while the floating window is open."],
    ["Webcam ... Sound only", "The four modes (chapter 8)."],
    ["Camera", "Front/back camera, current zoom, zoom in/out, zoom presets 0.5x/1x/1.5x/2x, "
               "resolution 1920 x 1080 / 1280 x 720, rotation 0/90/180/270 degrees."],
    ["Microphone", "Phone microphone on/off; <i>Standard</i> or <i>Noise suppression (phone)</i>."],
    ["Floating window", "Mirror, transparency (opaque, 15%, 30%), reset size and position."],
    ["Start with Windows", "Start WebbyLens automatically when you sign in."],
    ["Open log folder", "Log files for troubleshooting."],
    ["Exit", "Ends WebbyLens. The camera then disappears from Teams/Zoom until WebbyLens runs again."],
], [5.0, 11.4])

# 10
h1("10. The floating window")
p("The floating window shows the camera picture and stays on top of all other windows. It is "
  "<b>mirrored</b> by default, like a mirror (switch off in the menu); the picture sent to Teams/Zoom is "
  "never mirrored by WebbyLens.")
table([
    ["Action", "Result"],
    ["Drag with the mouse", "Move the window."],
    ["Drag an edge or corner", "Resize (the picture keeps its shape)."],
    ["Mouse wheel", "Make the window bigger or smaller, around the mouse pointer (up to 90% of the screen)."],
    ["Shift + mouse wheel", "Camera zoom in/out."],
    ["Ctrl + mouse wheel", "Transparency."],
    ["Double-click", "Full screen on/off."],
    ["Esc, wheel down, or double-click", "Leave full screen."],
    ["Right-click", "The menu (same as the tray icon)."],
    ["Alt+F4 / Close floating window", "Closes the window and switches to the same mode without window."],
], [5.6, 10.8])

# 11
h1("11. Camera, lenses and zoom")
bullets([
    "<b>Front or back camera:</b> menu <i>Camera</i> or the settings window.",
    "<b>Lenses:</b> many phones combine their back lenses into one camera. WebbyLens selects the lens by "
    "zoom: <b>1x</b> is the main lens, <b>0.5x</b> switches to the ultra-wide lens (if the phone has one). "
    "When crossing 1x, the phone switches lens and colours may jump briefly - this is normal.",
    "<b>Zoom</b> changes immediately, without interrupting the picture: Shift+wheel over the floating window, "
    "<i>Zoom in/out</i> in the menu, or the slider in the settings window. Each step is about 6%. "
    "The zoom is remembered.",
    "Above 1x the zoom is digital (a crop of the sensor). Up to about 2x the picture usually stays sharp; "
    "beyond about 3x it gets softer.",
    "<b>Rotation:</b> if the picture is sideways (phone held upright), choose a rotation in the menu.",
    "If the phone's own Camera app or another app on the phone uses the camera, WebbyLens cannot - close it.",
    "<b>Several phones:</b> WebbyLens remembers the camera, zoom, resolution, rotation and noise suppression "
    "separately for each phone. Plug in another phone and it starts with its own settings; a phone used for "
    "the first time starts with the settings currently in use. The <i>Camera</i> menu and the settings window "
    "show the name of the phone whose settings you are changing. With two phones plugged in at the same "
    "time, WebbyLens uses one of them - unplug the other.",
])

# 12
h1("12. Pause and automatic standby")
h2("Pause - Ctrl+Alt+P")
p("Press <b>Ctrl+Alt+P</b> anywhere (or use the menu) to pause. Press it again to resume.")
table([
    ["While paused", ""],
    ["Phone", "camera and microphone are off (saves battery)"],
    ["Teams/Zoom camera", "shows a picture with a pause symbol and the word <b>PAUSED</b>"],
    ["Teams/Zoom microphone", "<b>CABLE Output</b> is muted in Windows - Teams and Zoom usually show a notice "
                              "that the microphone is muted in the system"],
    ["Floating window", "closed; it comes back when you resume"],
    ["Tray icon", "orange"],
], [4.4, 12.0])
p("After resuming, picture and sound return within about 2 seconds; nothing has to be done in Teams or Zoom.")
box("Teams' own mute button does not change when you pause - Teams only sees that the device is muted. "
    "If WebbyLens is closed while paused, the microphone is unmuted again at its next start.")
h2("Automatic standby (\"Stream only when used\")")
p("By default the phone streams <b>only when needed</b>:")
bullets([
    "while a program receives the picture from the <b>WebbyLens</b> camera,",
    "while a program records from the <b>CABLE Output</b> microphone (e.g. Teams during a meeting - even when "
    "you are muted in Teams),",
    "while the <b>floating window</b> is open.",
])
p("When none of these is the case for 15 seconds, WebbyLens goes to standby (grey tray icon) and the phone "
  "rests. It wakes up by itself within a few seconds when a program needs it. Switch this off in the menu "
  "(<i>Stream only when used</i>) or in the settings if the phone should stream all the time.")

# 13
h1("13. Settings window and start with Windows")
p("Menu - <b>Settings...</b> shows all options in one place. Every change applies immediately:")
table([
    ["Setting", "Description"],
    ["Camera", "Front or back camera."],
    ["Zoom", "Slider from 0.5x to about 8x, live."],
    ["Resolution", "1920 x 1080 or 1280 x 720 (what the phone sends)."],
    ["Rotation", "0, 90, 180 or 270 degrees."],
    ["Microphone", "Phone microphone on/off; noise suppression by the phone."],
    ["Window transparency", "0-80% transparent."],
    ["Mirror", "Mirror the floating window."],
    ["Phone", "Stream only when used (automatic standby)."],
], [4.4, 12.0])
p("Camera, zoom, resolution, rotation and noise suppression are remembered for each phone separately "
  "(see chapter 11); the other settings apply to all phones.")
p("<b>Start with Windows:</b> chosen during installation; can be changed any time in the menu. Settings "
  "are stored per Windows user.")

# 14
h1("14. Troubleshooting")
table([
    ["Problem", "What to do"],
    ["I cannot find the tray icon.", "Click the <b>^</b> arrow next to the clock. If it is not there, start WebbyLens "
                                     "from the Start menu. A message \"already running\" means it is running - look again "
                                     "in the hidden icons."],
    ["\"No phone found\"", "Check the cable (data cable), unlock the phone, check that USB debugging is on (chapter 3)."],
    ["\"Phone ... is not authorized\"", "Unlock the phone and accept \"Allow USB debugging\" (tick \"Always allow\"). "
                                        "If no dialog appears, unplug and plug in the cable."],
    ["\"The phone camera is used by another app\"", "Close the Camera app or video-call app on the phone."],
    ["Teams/Zoom shows a crossed-out phone", "WebbyLens has no picture yet: wait a few seconds (standby wake-up), "
                                             "check the status in the tray icon, check mode is not \"Floating window only\" "
                                             "or \"Sound only\"."],
    ["Teams/Zoom shows \"PAUSED\"", "Press Ctrl+Alt+P to resume."],
    ["The camera WebbyLens is not listed", "WebbyLens must be running (it creates the camera). Restart the program "
                                           "(Teams/Zoom). If it is still missing, run the installer again (repair)."],
    ["Nobody hears me", "Select <b>CABLE Output</b> as microphone; check that VB-Cable is installed and the menu item "
                        "<i>Microphone - Phone microphone on</i> is ticked; check WebbyLens is not paused."],
    ["Background noise", "Menu <i>Microphone - Noise suppression (phone)</i>."],
    ["Picture sideways", "Menu <i>Camera - Rotation</i>."],
    ["Picture too wide / too close", "Zoom (Shift+wheel, menu, settings)."],
    ["Something else", "Menu <i>Open log folder</i>. Logs: <i>%LOCALAPPDATA%\\WebbyLens\\logs</i> (program) and "
                       "<i>%ProgramData%\\WebbyLens\\logs</i> (camera). Please attach them when reporting a problem "
                       "on GitHub."],
], [5.2, 11.2])
box("Start WebbyLens normally, <b>not</b> \"as administrator\". The installer does this correctly by itself.", "warn")

# 15
h1("15. Privacy")
bullets([
    "WebbyLens does not send any data anywhere. Picture and sound go only from the phone through the USB "
    "cable to your PC and to the programs that you choose.",
    "WebbyLens has no account, no telemetry and does not use the internet. Only the installer downloads "
    "Google's adb tool from dl.google.com.",
    "The phone side is the open-source scrcpy server; it is copied to the phone's temporary folder each time "
    "and removed again when the connection ends.",
    "Logs contain technical information (phone model, settings, errors), no pictures or sound, and stay on your PC.",
])

# 16
h1("16. Uninstall")
p("Open <b>Settings - Apps - Installed apps</b>, find <b>WebbyLens</b> and choose <b>Uninstall</b>. "
  "WebbyLens is closed, the virtual camera is removed, and you are asked whether your settings and logs "
  "should be deleted as well. VB-Audio Virtual Cable is a separate product: uninstall it from its own "
  "setup program if you no longer need it. Switch off USB debugging on the phone if you no longer use it.")

# 17
h1("17. License and credits")
p("<b>Authors:</b> Damir Vrancic and Claude. Project page, updates and bug reports: <b>https://github.com/crunchy3d/WebbyLens</b>")
p("WebbyLens is free and open-source software under the <b>MIT License</b>. It uses:")
bullets([
    "<b>scrcpy server</b> by Genymobile and Romain Vimont (Apache License 2.0), shipped unmodified - "
    "https://github.com/Genymobile/scrcpy",
    "<b>Android SDK Platform-Tools (adb)</b> by Google, downloaded during installation under Google's terms.",
    "<b>VB-Audio Virtual Cable</b> by VB-Audio Software (donationware), installed separately by the user.",
    "Simon Mourier's <b>VCamSample</b> (MIT) served as a reference for the Windows 11 virtual camera.",
])
p("Details: <i>licenses</i> folder in the installation directory and THIRD_PARTY_NOTICES.md in the project. "
  "WebbyLens is not affiliated with Google, Android, scrcpy, Microsoft, Zoom or VB-Audio.")


# ---------------------------------------------------------------- page decoration
def decorate(canvas, doc):
    canvas.saveState()
    if doc.page > 1:
        canvas.setStrokeColor(GRID)
        canvas.setLineWidth(0.5)
        canvas.line(2 * cm, 1.5 * cm, A4[0] - 2 * cm, 1.5 * cm)
        canvas.setFont("Helvetica", 8.5)
        canvas.setFillColor(colors.Color(0.4, 0.43, 0.5))
        canvas.drawString(2 * cm, 1.05 * cm, f"WebbyLens {VERSION} - User Guide")
        canvas.drawRightString(A4[0] - 2 * cm, 1.05 * cm, f"Page {doc.page}")
    canvas.restoreState()


def main():
    OUT.parent.mkdir(exist_ok=True)
    doc = SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=2 * cm, rightMargin=2 * cm,
                            topMargin=2 * cm, bottomMargin=2.2 * cm,
                            title="WebbyLens User Guide", author="Damir Vrancic and Claude",
                            subject=f"WebbyLens {VERSION}")
    doc.build(story, onFirstPage=decorate, onLaterPages=decorate)
    print("written:", OUT)


if __name__ == "__main__":
    main()
