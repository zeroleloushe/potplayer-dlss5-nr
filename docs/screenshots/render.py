#!/usr/bin/env python3
"""Raster mockups of the installer / settings UI for the GitHub release."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = Path(__file__).parent
BG = (18, 18, 20)
BG2 = (28, 28, 32)
BG3 = (12, 12, 14)
FG = (245, 245, 247)
MUTED = (160, 160, 168)
ACCENT = (118, 185, 0)  # NVIDIA green
ACCENT2 = (196, 165, 116)
RED = (232, 88, 88)
TRACK = (55, 55, 62)
WHITE = (255, 255, 255)
CHK = (40, 40, 46)

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONTM = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"


def fnt(size, bold=False, mono=False):
    path = FONTM if mono else (FONTB if bold else FONT)
    return ImageFont.truetype(path, size)


def rr(draw, box, r, fill=None, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def chrome(img, title, w, h, title_h=36):
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w, h], fill=BG3)
    d.rectangle([0, 0, w, title_h], fill=(32, 32, 36))
    d.ellipse([14, 12, 26, 24], fill=(255, 95, 86))
    d.ellipse([34, 12, 46, 24], fill=(255, 189, 46))
    d.ellipse([54, 12, 66, 24], fill=(39, 201, 63))
    d.text((84, 8), title, font=fnt(16), fill=MUTED)
    return title_h


def checkbox(d, x, y, text, on=True):
    rr(d, [x, y, x + 18, y + 18], 3, fill=ACCENT if on else CHK, outline=(80, 80, 88))
    if on:
        d.line([(x + 4, y + 10), (x + 7, y + 14), (x + 14, y + 5)], fill=BG3, width=2)
    d.text((x + 28, y - 1), text, font=fnt(15), fill=FG)


def trackbar(d, x, y, w, value, vmax=2.0):
    # value 0..vmax
    d.rounded_rectangle([x, y + 8, x + w, y + 14], radius=3, fill=TRACK)
    pos = int(w * (value / vmax))
    d.rounded_rectangle([x, y + 8, x + pos, y + 14], radius=3, fill=ACCENT)
    cx = x + pos
    d.ellipse([cx - 9, y + 2, cx + 9, y + 20], fill=FG, outline=(200, 200, 204))


def combo(d, x, y, w, text):
    rr(d, [x, y, x + w, y + 28], 4, fill=BG2, outline=(60, 60, 68))
    d.text((x + 10, y + 5), text, font=fnt(14), fill=FG)
    d.polygon([(x + w - 18, y + 10), (x + w - 8, y + 10), (x + w - 13, y + 18)], fill=MUTED)


def button(d, x, y, w, h, text, primary=False):
    rr(d, [x, y, x + w, y + h], 6, fill=ACCENT if primary else BG2, outline=None if primary else (70, 70, 78))
    tw = d.textlength(text, font=fnt(15, bold=True))
    d.text((x + (w - tw) / 2, y + (h - 18) / 2), text, font=fnt(15, bold=True), fill=BG3 if primary else FG)


def edit(d, x, y, w, text):
    rr(d, [x, y, x + w, y + 28], 4, fill=(10, 10, 12), outline=(70, 70, 78))
    d.text((x + 8, y + 5), text, font=fnt(13, mono=True), fill=FG)


def render_installer():
    W, H = 1080, 960
    img = Image.new("RGB", (W, H), BG3)
    th = chrome(img, "DLSS 5 NR  —  установка", W, H)
    d = ImageDraw.Draw(img)
    # body
    d.rectangle([0, th, W, H], fill=BG)
    d.text((40, th + 28), "DLSS 5 Neural Rendering", font=fnt(28, bold=True), fill=FG)
    d.text((40, th + 28 + 36), "для PotPlayer + SVP", font=fnt(20), fill=ACCENT)
    d.text((40, th + 100), "Укажи папку портативного PotPlayer (где PotPlayerMini64.exe).", font=fnt(16), fill=MUTED)

    d.text((40, th + 148), "Папка", font=fnt(15), fill=MUTED)
    edit(d, 130, th + 144, 700, r"C:\Users\leloushe\PotPlayer")
    button(d, 850, th + 142, 180, 32, "Обзор…")

    checkbox(d, 40, th + 204, "Зарегистрировать настройки в Менеджере фильтров", True)
    checkbox(d, 40, th + 244, "Скопировать профили GPU-3-High-NR / GPU-NR", True)
    checkbox(d, 40, th + 284, "Заменить svp.avs (старый уйдёт в .bak)", False)

    button(d, 40, th + 340, 280, 48, "Установить", primary=True)

    rr(d, [40, th + 412, W - 40, H - 40], 8, fill=(10, 10, 12), outline=(40, 40, 46))
    log = [
        "NVIDIA nvngx_dlssnr.dll установщик не кладёт — его надо взять из игры.",
        "Установка...",
        "svpflow1.dll → C:\\Users\\leloushe\\PotPlayer\\AviSynth",
        "  DLSS5NR.dll",
        "  nvngx.dll_pot.dll",
        "  nr-settings.exe",
        "фильтр DLSS 5 NR зарегистрирован",
        "Готово. В PotPlayer: AviSynth-скрипт GPU-3-High-NR.avs",
    ]
    y = th + 428
    for i, line in enumerate(log):
        col = ACCENT if i == 0 or "Готово" in line else MUTED
        if line.startswith("  "):
            col = FG
        d.text((56, y), line, font=fnt(14, mono=True), fill=col)
        y += 22
    img.save(OUT / "installer.png", "PNG", optimize=True)


def render_settings():
    W, H = 900, 1120
    img = Image.new("RGB", (W, H), BG3)
    th = chrome(img, "DLSS 5 NR - настройки", W, H)
    d = ImageDraw.Draw(img)
    d.rectangle([0, th, W, H], fill=BG)
    d.text((40, th + 24), "DLSS 5 Neural Rendering", font=fnt(26, bold=True), fill=FG)
    d.text((40, th + 64), "Крутится на лету, ролик перезапускать не нужно.", font=fnt(15), fill=MUTED)

    d.text((40, th + 112), "Стиль", font=fnt(15), fill=MUTED)
    combo(d, 280, th + 106, 540, "1  Natural")
    d.text((40, th + 160), "Пресет", font=fnt(15), fill=MUTED)
    combo(d, 280, th + 154, 540, "0")

    sliders = [
        ("Интенсивность (степень)", 1.00),
        ("Тон", 1.01),
        ("Структура", 1.50),
        ("Кожа", 2.00),
    ]
    y = th + 220
    for name, val in sliders:
        d.text((40, y), name, font=fnt(16), fill=FG)
        d.text((760, y), f"{val:.2f}", font=fnt(16, bold=True), fill=ACCENT)
        trackbar(d, 40, y + 28, 780, val, 2.0)
        y += 100

    checkbox(d, 40, y + 8, "Автомаска (кожа)", True)
    checkbox(d, 480, y + 8, "Поверх окон", True)
    button(d, 40, y + 56, 200, 40, "Сброс")
    d.text((40, y + 116), "INI: %LOCALAPPDATA%\\potplayer-dlss5-nr\\settings.ini", font=fnt(13, mono=True), fill=MUTED)
    img.save(OUT / "settings.png", "PNG", optimize=True)


def render_pipeline():
    W, H = 1400, 520
    img = Image.new("RGB", (W, H), (9, 9, 11))
    d = ImageDraw.Draw(img)
    d.text((48, 28), "Как это стоит в графе", font=fnt(22, bold=True), fill=FG)
    d.text((48, 62), "NR считает исходные кадры в фоне — звук не уезжает при перемотке", font=fnt(15), fill=MUTED)

    boxes = [
        ("Декодер", "DXVA copy-back", (70, 70, 78)),
        ("DLSS 5 NR", "AviSynth · async GPU", ACCENT),
        ("SVP", "интерполяция до 60", (70, 110, 180)),
        ("Рендер", "D3D11 / madVR", (70, 70, 78)),
    ]
    x = 48
    y = 140
    bw, bh, gap = 280, 160, 40
    for i, (title, sub, col) in enumerate(boxes):
        rr(d, [x, y, x + bw, y + bh], 16, fill=(22, 22, 26), outline=col, width=3)
        d.text((x + 24, y + 40), title, font=fnt(22, bold=True), fill=FG)
        d.text((x + 24, y + 84), sub, font=fnt(15), fill=MUTED)
        if i < len(boxes) - 1:
            ax = x + bw + 8
            d.polygon([(ax, y + bh / 2 - 8), (ax + 24, y + bh / 2), (ax, y + bh / 2 + 8)], fill=ACCENT)
        x += bw + gap
    d.text((48, 360), "Профили AviSynth", font=fnt(16, bold=True), fill=FG)
    chips = [
        ("GPU-3-High", "только SVP 60", False),
        ("GPU-NR", "только DLSS 5", True),
        ("GPU-3-High-NR", "NR + SVP", True),
    ]
    x = 48
    for name, sub, hi in chips:
        col = ACCENT if hi else (60, 60, 68)
        rr(d, [x, 400, x + 400, 480], 12, fill=(22, 22, 26), outline=col, width=2)
        d.text((x + 20, 416), name, font=fnt(18, bold=True), fill=FG)
        d.text((x + 20, 446), sub, font=fnt(14), fill=MUTED)
        x += 430
    img.save(OUT / "pipeline.png", "PNG", optimize=True)


def render_hero():
    W, H = 1280, 640
    img = Image.new("RGB", (W, H), (9, 9, 11))
    # vignette-ish gradient
    overlay = Image.new("RGB", (W, H), (9, 9, 11))
    od = ImageDraw.Draw(overlay)
    for i in range(H):
        t = i / H
        g = int(9 + 18 * (1 - t))
        a = int(0 + 22 * t)
        od.line([(0, i), (W, i)], fill=(g, g + 2, a))
    img = Image.blend(img, overlay, 0.85)
    # glow blob
    blob = Image.new("L", (W, H), 0)
    bd = ImageDraw.Draw(blob)
    bd.ellipse([700, -80, 1400, 520], fill=90)
    blob = blob.filter(ImageFilter.GaussianBlur(80))
    tint = Image.new("RGB", (W, H), ACCENT)
    img.paste(tint, mask=blob)

    d = ImageDraw.Draw(img)
    d.text((64, 160), "DLSS 5 NR", font=fnt(64, bold=True), fill=WHITE)
    d.text((64, 240), "для PotPlayer + SVP", font=fnt(36), fill=ACCENT)
    d.text((64, 310), "Нейро-шумодав NVIDIA в портативном плеере.", font=fnt(20), fill=FG)
    d.text((64, 348), "Один установщик · настройки на лету · без рассинхрона на seek.", font=fnt(18), fill=MUTED)

    # fake window thumbnail
    tw, th = 420, 260
    tx, ty = 800, 200
    rr(d, [tx, ty, tx + tw, ty + th], 12, fill=BG, outline=(50, 50, 56), width=2)
    d.rectangle([tx, ty, tx + tw, ty + 32], fill=(32, 32, 36))
    d.text((tx + 14, ty + 8), "nr-settings", font=fnt(13), fill=MUTED)
    trackbar(d, tx + 24, ty + 80, 370, 1.0)
    trackbar(d, tx + 24, ty + 120, 370, 1.0)
    trackbar(d, tx + 24, ty + 160, 370, 1.5)
    trackbar(d, tx + 24, ty + 200, 370, 2.0)

    d.text((64, 560), "Windows x64  ·  RTX  ·  AviSynth+  ·  GPL-2.0", font=fnt(14), fill=MUTED)
    img.save(OUT / "hero.png", "PNG", optimize=True)


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    render_hero()
    render_installer()
    render_settings()
    render_pipeline()
    for p in OUT.glob("*.png"):
        print(p.name, p.stat().st_size, Image.open(p).size)
