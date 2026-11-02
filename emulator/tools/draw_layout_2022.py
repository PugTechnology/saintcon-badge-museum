#!/usr/bin/env python3
"""
Generate the SAINTCON 2022 Clue-style Notepad Badge board drawing (board.png),
interactive geometry (layout.json), and metadata (badge.json).

Drawn from an owner's photograph of a real 2022 badge (not from CAD/board files,
which were not published upstream), so component positions are approximate.
"""

import json
import math
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT_DIR / "emulator" / "web" / "badges" / "2022"

SUSPECTS = [
    "Anonymous",
    "Chunk",
    "Compukidmike",
    "C0n4rt1st",
    "D4rkm4tter",
    "Edward Snowden",
    "Fyodor",
    "HD Moore",
    "Jup1t3r",
    "Kingpin",
    "L34N",
    "Linus Torvalds",
    "LostboY",
    "Pali(?)",
    "Ray-Man",
    "sirashrum",
    "Supertechguy",
    "The Dark Tangent",
    "Woz",
    "Zevlag",
    "Zodiak",
]

ATTACKS = [
    "Buffer Overflow",
    "Cross-Site Request Forgery",
    "Default Password",
    "Denial-of-Service",
    "DNS Spoofing",
    "Heap Overflow",
    "Heartbleed",
    "Metasploit",
    "Pass-the-Hash",
    "Phishing",
    "Polymorphic Virus",
    "Privilege Escalation",
    "Ransomware",
    "Remote Code Execution",
    "RootKit",
    "ShadowBrokers",
    "Social Engineering",
    "Speculative Execution",
    "SQL Injection",
    "Zero-Click",
    "Zero-Day",
]

ROOMS = [
    "Badgelife",
    "Ballrooms",
    "Hacker Lounge",
    "Infobooth",
    "Lockpicking",
    "Mezzanine",
    "Registration",
    "Terrace",
    "The Cloud",
    "Trainings",
]

# Exact charlieplex pin pairs extracted from Saintcon2022BadgeFirmwareFix.uf2 flash tables:
# Suspects: 21 entries at 0x1000b60c -> RAM 0x20000888
# Attacks:  21 entries at 0x1000b314 -> RAM 0x20000590
# Rooms:    10 entries at 0x1000b514 -> RAM 0x20000790
UF2_PATH = ROOT_DIR / "emulator" / "cores" / "2022" / "upstream" / "Saintcon2022BadgeFirmwareFix.uf2"


def extract_led_tables_from_uf2(uf2_path: Path):
    data = uf2_path.read_bytes()
    flash = bytearray(65536)
    for off in range(0, len(data), 512):
        magic = int.from_bytes(data[off : off + 4], "little")
        if magic != 0x0A324655:
            continue
        addr = int.from_bytes(data[off + 12 : off + 16], "little")
        size = int.from_bytes(data[off + 16 : off + 20], "little")
        rel = addr - 0x10000000
        if 0 <= rel < len(flash):
            flash[rel : rel + size] = data[off + 32 : off + 32 + size]

    tables = [
        ("suspects", 0xB60C, 21, 23, SUSPECTS),
        ("attacks", 0xB314, 21, 23, ATTACKS),
        ("rooms", 0xB514, 10, 15, ROOMS),
    ]
    entries = []
    for cat, base_off, count, msb, labels in tables:
        for i in range(count):
            row_off = base_off + i * 24
            b_cat, b_ano = flash[row_off + 0], flash[row_off + 1]
            r_cat, r_ano = flash[row_off + 8], flash[row_off + 9]
            g_cat, g_ano = flash[row_off + 16], flash[row_off + 17]
            entries.append(
                {
                    "index": len(entries),
                    "category": cat,
                    "row": i,
                    "label": labels[i],
                    "mask_bit": msb - i,
                    "r": {"cathode": r_cat, "anode": r_ano},
                    "g": {"cathode": g_cat, "anode": g_ano},
                    "b": {"cathode": b_cat, "anode": b_ano},
                }
            )
    return entries


def load_font(size: int, bold: bool = False):
    candidates = (
        [
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        ]
        if bold
        else [
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        ]
    )
    for p in candidates:
        if os.path.exists(p):
            return ImageFont.truetype(p, size=size)
    return ImageFont.load_default()


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    led_meta = extract_led_tables_from_uf2(UF2_PATH)

    # Canvas geometry (1200 x 1460 px)
    W, H = 1200, 1460
    SCALE = 10.0  # 10 px per approximate mm
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Colors matched to the real 2022 notepad badge
    TEAL = (0, 138, 128, 255)
    TEAL_LIGHT = (0, 156, 145, 255)
    WHITE_PCB = (246, 248, 247, 255)
    PCB_BORDER = (210, 216, 214, 255)
    WING_PCB = (24, 26, 28, 255)
    WING_BORDER = (58, 62, 66, 255)
    HEADER_PLASTIC = (14, 15, 17, 255)
    GOLD_PAD = (214, 176, 76, 255)
    SILVER_PAD = (196, 202, 206, 255)
    COIL_YELLOW = (216, 232, 18, 235)
    COIL_SHADOW = (154, 172, 10, 220)
    IC_BODY = (34, 37, 40, 255)
    BTN_BODY = (42, 45, 48, 255)
    BTN_BLUE = (36, 118, 210, 255)
    BTN_RED = (214, 72, 48, 255)

    # Main white notepad PCB bounds
    board_x0, board_y0 = 230, 86
    board_x1, board_y1 = 970, 1416

    # Left and right black minibadge wings (aligned with the 21+5 row grid)
    wing_top, wing_bot = 150, 910
    left_wing_x0, left_wing_x1 = 26, 226
    right_wing_x0, right_wing_x1 = 974, 1174

    # Draw left and right wings (4 minibadge holders per wing, 8 total)
    for is_left, (wx0, wx1) in enumerate([(left_wing_x0, left_wing_x1), (right_wing_x0, right_wing_x1)]):
        draw.rounded_rectangle(
            [wx0, wing_top, wx1, wing_bot],
            radius=10,
            fill=WING_PCB,
            outline=WING_BORDER,
            width=2,
        )
        # 4 minibadge slots per wing (each has top and bottom 1x8 female headers
        # with a silver rectangular pad visible between them)
        slot_ys = [164, 348, 532, 716]
        for sy in slot_ys:
            # Silver rectangle visible between the top and bottom 1x8 headers
            draw.rounded_rectangle(
                [wx0 + 22, sy + 32, wx1 - 22, sy + 132],
                radius=3,
                fill=SILVER_PAD,
                outline=(142, 148, 152, 255),
                width=1,
            )
            for hy in [sy + 4, sy + 136]:
                draw.rectangle(
                    [wx0 + 16, hy, wx1 - 16, hy + 24],
                    fill=HEADER_PLASTIC,
                    outline=(45, 48, 52, 255),
                    width=1,
                )
                # 8 socket holes across each header
                for pin in range(8):
                    px = wx0 + 28 + pin * 20
                    draw.rectangle(
                        [px - 4, hy + 7, px + 4, hy + 17],
                        fill=(68, 72, 76, 255),
                        outline=SILVER_PAD,
                        width=1,
                    )
        # Unpopulated outer 3x1 through-hole pads on the outer edge of each wing
        outer_x = wx0 + 8 if is_left == 0 else wx1 - 8
        for pad_y in [190, 760]:
            draw.rectangle(
                [outer_x - 6, pad_y - 4, outer_x + 6, pad_y + 40],
                outline=SILVER_PAD,
                width=1,
            )
            for k in range(3):
                py = pad_y + k * 14 + 4
                draw.ellipse(
                    [outer_x - 3, py - 3, outer_x + 3, py + 3],
                    fill=(20, 22, 24, 255),
                    outline=SILVER_PAD,
                    width=1,
                )

    # Two 3x1 pin headers per wing connecting wings to main board
    for bridge_y in [190, 760]:
        for bx in [board_x0 - 8, board_x1 - 4]:
            for k in range(3):
                by = bridge_y + k * 14
                draw.rounded_rectangle(
                    [bx, by, bx + 12, by + 9],
                    radius=3,
                    fill=SILVER_PAD,
                    outline=(140, 145, 148, 255),
                    width=1,
                )

    # Draw main white notepad PCB
    draw.rounded_rectangle(
        [board_x0, board_y0, board_x1, board_y1],
        radius=12,
        fill=WHITE_PCB,
        outline=PCB_BORDER,
        width=3,
    )

    # Top spiral coil holes and yellow/green translucent coil loops
    hole_count = 14
    hole_x_start = board_x0 + 36
    hole_x_step = (board_x1 - board_x0 - 72) / (hole_count - 1)
    hole_y = board_y0 + 24
    for i in range(hole_count):
        hx = hole_x_start + i * hole_x_step
        # Punch hole in PCB
        draw.ellipse(
            [hx - 9, hole_y - 9, hx + 9, hole_y + 9],
            fill=(32, 36, 40, 255),
            outline=(180, 186, 184, 255),
            width=2,
        )
        # Back coil arc
        draw.line(
            [(hx - 6, hole_y - 2), (hx + 10, board_y0 - 34)],
            fill=COIL_SHADOW,
            width=9,
        )
        # Front neon yellow-green spiral loop
        draw.line(
            [(hx - 10, board_y0 - 34), (hx - 1, hole_y + 2)],
            fill=COIL_YELLOW,
            width=10,
        )
        draw.ellipse(
            [hx - 14, board_y0 - 40, hx + 12, board_y0 - 24],
            outline=COIL_YELLOW,
            width=6,
        )

    # Notepad grid geometry
    grid_top = 136
    row_h = 31
    suspects_rows = 21
    rooms_rows = 5
    divider_y = grid_top + suspects_rows * row_h  # 136 + 651 = 787
    grid_bot = divider_y + rooms_rows * row_h  # 787 + 155 = 942

    # X columns of the notepad table
    col_left_outer = board_x0 + 12          # 242
    col_left_cat = board_x0 + 40            # 270 (end of vertical SUSPECTS / ROOMS strip)
    col_left_led = board_x0 + 74            # 304 (end of left LED column)
    col_mid_split = (board_x0 + board_x1) // 2 - 8  # 592 (end of Suspects text / start of Attacks LEDs)
    col_right_led = col_mid_split + 34      # 626 (end of Attacks LED column)
    col_right_cat = board_x1 - 40           # 930 (start of vertical ATTACKS/EXPLOITS / ROOMS strip)
    col_right_outer = board_x1 - 12         # 958

    # Outer table frame & vertical column lines in teal
    draw.rectangle(
        [col_left_outer, grid_top, col_right_outer, grid_bot],
        outline=TEAL,
        width=3,
    )
    for vx in [col_left_cat, col_left_led, col_mid_split, col_right_led, col_right_cat]:
        draw.line([(vx, grid_top), (vx, grid_bot)], fill=TEAL, width=2)

    # Thicker horizontal divider between Suspects/Attacks and Rooms
    draw.line([(col_left_outer, divider_y), (col_right_outer, divider_y)], fill=TEAL, width=4)

    font_label = load_font(19, bold=False)
    font_label_small = load_font(17, bold=False)
    font_vert = load_font(18, bold=True)
    font_vert_small = load_font(15, bold=True)

    led_positions = {}  # (category, row) -> (cx, cy)

    def draw_led_package(cx: int, cy: int):
        # 4-pad SMD RGB LED footprint (white ceramic package + gold side pads)
        draw.rectangle([cx - 10, cy - 8, cx + 10, cy + 8], fill=GOLD_PAD)
        draw.rectangle(
            [cx - 7, cy - 8, cx + 7, cy + 8],
            fill=(248, 246, 236, 255),
            outline=(155, 148, 128, 255),
            width=1,
        )
        draw.ellipse(
            [cx - 4, cy - 4, cx + 4, cy + 4],
            fill=(228, 224, 205, 255),
            outline=(180, 172, 150, 255),
            width=1,
        )

    # Draw 21 Suspects & 21 Attacks rows
    for r in range(suspects_rows):
        y0 = grid_top + r * row_h
        y1 = y0 + row_h
        cy = (y0 + y1) // 2
        if r > 0:
            draw.line([(col_left_cat, y0), (col_right_cat, y0)], fill=TEAL_LIGHT, width=1)

        # Left LED (Suspects r)
        s_led_x = (col_left_cat + col_left_led) // 2
        draw_led_package(s_led_x, cy)
        led_positions[("suspects", r)] = (s_led_x, cy)

        # Suspect label
        s_text = SUSPECTS[r]
        draw.text((col_left_led + 8, y0 + 5), s_text, fill=TEAL, font=font_label)

        # Small ambient light sensor IC on row 12 ("LostboY") right edge, matching owner's photo
        if r == 12:
            als_x, als_y = col_mid_split - 18, cy
            draw.rectangle([als_x - 8, als_y - 9, als_x + 8, als_y + 9], fill=GOLD_PAD)
            draw.rectangle(
                [als_x - 6, als_y - 9, als_x + 6, als_y + 9],
                fill=IC_BODY,
                outline=(80, 84, 88, 255),
                width=1,
            )

        # Right LED (Attacks r)
        a_led_x = (col_mid_split + col_right_led) // 2
        draw_led_package(a_led_x, cy)
        led_positions[("attacks", r)] = (a_led_x, cy)

        # Attack label
        a_text = ATTACKS[r]
        f_use = font_label_small if len(a_text) >= 24 else font_label
        draw.text((col_right_led + 8, y0 + (6 if f_use == font_label_small else 5)), a_text, fill=TEAL, font=f_use)

    # Draw 5 Rooms rows (split 0..4 on left, 5..9 on right)
    for r in range(rooms_rows):
        y0 = divider_y + r * row_h
        y1 = y0 + row_h
        cy = (y0 + y1) // 2
        if r > 0:
            draw.line([(col_left_cat, y0), (col_right_cat, y0)], fill=TEAL_LIGHT, width=1)

        # Left Room (0..4)
        rl_led_x = (col_left_cat + col_left_led) // 2
        draw_led_package(rl_led_x, cy)
        led_positions[("rooms", r)] = (rl_led_x, cy)
        draw.text((col_left_led + 8, y0 + 5), ROOMS[r], fill=TEAL, font=font_label)

        # Right Room (5..9)
        rr_led_x = (col_mid_split + col_right_led) // 2
        draw_led_package(rr_led_x, cy)
        led_positions[("rooms", r + 5)] = (rr_led_x, cy)
        draw.text((col_right_led + 8, y0 + 5), ROOMS[r + 5], fill=TEAL, font=font_label)

    # Vertical category labels along the outer strips
    def draw_vertical_stack(text: str, cx: int, y_start: int, y_end: int, font):
        n = len(text)
        step = (y_end - y_start) / max(1, n)
        for idx, ch in enumerate(text):
            cy = y_start + (idx + 0.5) * step
            bbox = draw.textbbox((0, 0), ch, font=font)
            tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
            draw.text((cx - tw / 2, cy - th / 2 - 2), ch, fill=TEAL, font=font)

    draw_vertical_stack("SUSPECTS", (col_left_outer + col_left_cat) // 2, grid_top + 130, divider_y - 130, font_vert)
    draw_vertical_stack(
        "ATTACKS/EXPLOITS",
        (col_right_cat + col_right_outer) // 2,
        grid_top + 60,
        divider_y - 60,
        font_vert_small,
    )
    draw_vertical_stack("ROOMS", (col_left_outer + col_left_cat) // 2, divider_y + 10, grid_bot - 10, font_vert)
    draw_vertical_stack("ROOMS", (col_right_cat + col_right_outer) // 2, divider_y + 10, grid_bot - 10, font_vert)

    # Bottom section: SAINTCON logo text
    font_logo = load_font(44, bold=True)
    draw.text((board_x0 + 36, 956), "SAINTCON", fill=TEAL, font=font_logo)

    # Tactile buttons: UP (GP15), DOWN (GP16), SELECT (GP17)
    font_btn = load_font(20, bold=True)
    font_ref = load_font(13, bold=False)

    def draw_tactile_button(cx: int, cy: int, cap_color, label: str, label_above: bool = True):
        # 4 gull-wing SMD legs
        for ly in [cy - 18, cy + 12]:
            draw.rectangle([cx - 34, ly, cx + 34, ly + 6], fill=SILVER_PAD, outline=(140, 145, 148, 255), width=1)
        # Switch housing
        draw.rounded_rectangle(
            [cx - 26, cy - 26, cx + 26, cy + 26],
            radius=5,
            fill=BTN_BODY,
            outline=(90, 96, 102, 255),
            width=2,
        )
        # Corner metallic tabs
        for tx, ty in [(cx - 22, cy - 22), (cx + 16, cy - 22), (cx - 22, cy + 16), (cx + 16, cy + 16)]:
            draw.rectangle([tx, ty, tx + 6, ty + 6], fill=SILVER_PAD)
        # Round colored button cap
        draw.ellipse(
            [cx - 16, cy - 16, cx + 16, cy + 16],
            fill=cap_color,
            outline=(20, 24, 28, 255),
            width=2,
        )
        bbox = draw.textbbox((0, 0), label, font=font_btn)
        tw = bbox[2] - bbox[0]
        ly_text = cy - 52 if label_above else cy + 32
        draw.text((cx - tw / 2, ly_text), label, fill=TEAL, font=font_btn)

    up_center = (296, 1110)
    down_center = (296, 1218)
    select_center = (902, 1126)

    draw_tactile_button(up_center[0], up_center[1], BTN_BLUE, "UP", label_above=True)
    draw_tactile_button(down_center[0], down_center[1], BTN_BLUE, "DOWN", label_above=False)
    draw_tactile_button(select_center[0], select_center[1], BTN_RED, "SELECT", label_above=True)

    # Center-left front silver minibadge pad area (between UP/DOWN and RP2040)
    exp_x0, exp_y0, exp_x1, exp_y1 = 368, 1068, 560, 1244
    draw.rounded_rectangle(
        [exp_x0, exp_y0, exp_x1, exp_y1],
        radius=4,
        fill=SILVER_PAD,
        outline=(140, 146, 150, 255),
        width=2,
    )
    draw.rectangle(
        [exp_x0 + 6, exp_y0 + 6, exp_x1 - 6, exp_y1 - 6],
        outline=(218, 224, 228, 255),
        width=1,
    )
    for pin in range(8):
        px = exp_x0 + 16 + pin * 23
        for py in [exp_y0 - 14, exp_y1 + 14]:
            draw.ellipse([px - 7, py - 7, px + 7, py + 7], fill=(40, 44, 48, 255), outline=SILVER_PAD, width=2)

    # RP2040 (U1), QSPI Flash SOIC-8 (U4), LDO Regulator, plain U6 block, and Crystal (Y1)
    u1_cx, u1_cy = 678, 1132
    draw.rectangle([u1_cx - 38, u1_cy - 38, u1_cx + 38, u1_cy + 38], fill=IC_BODY, outline=(80, 84, 88, 255), width=2)
    draw.ellipse([u1_cx - 28, u1_cy - 28, u1_cx - 20, u1_cy - 20], fill=(120, 124, 128, 255))
    draw.text((u1_cx - 26, u1_cy - 8), "RP2040", fill=(165, 170, 175, 255), font=font_ref)
    draw.text((u1_cx + 42, u1_cy - 36), "U1", fill=TEAL, font=font_ref)

    # U4 (SOIC-8 Flash)
    u4_cx, u4_cy = 772, 1066
    for p in range(4):
        py = u4_cy - 18 + p * 11
        draw.rectangle([u4_cx - 28, py, u4_cx + 28, py + 5], fill=SILVER_PAD)
    draw.rectangle([u4_cx - 20, u4_cy - 24, u4_cx + 20, u4_cy + 24], fill=IC_BODY, outline=(80, 84, 88, 255), width=1)
    draw.text((u4_cx - 10, u4_cy - 40), "U4", fill=TEAL, font=font_ref)

    # SOT-223 Regulator (bottom-left of RP2040) and plain U6 block (below RP2040)
    reg_cx, reg_cy = 636, 1230
    draw.rectangle([reg_cx + 14, reg_cy - 12, reg_cx + 24, reg_cy + 12], fill=SILVER_PAD)
    for py in [reg_cy - 14, reg_cy - 2, reg_cy + 10]:
        draw.rectangle([reg_cx - 24, py, reg_cx - 14, py + 4], fill=SILVER_PAD)
    draw.rectangle([reg_cx - 16, reg_cy - 22, reg_cx + 16, reg_cy + 22], fill=IC_BODY, outline=(80, 84, 88, 255), width=1)

    u6_cx, u6_cy = 738, 1236
    draw.rectangle([u6_cx - 12, u6_cy - 9, u6_cx + 12, u6_cy + 9], fill=IC_BODY, outline=(80, 84, 88, 255), width=1)
    draw.text((u6_cx - 10, u6_cy - 24), "U6", fill=TEAL, font=font_ref)

    # Y1 (Crystal)
    y1_cx, y1_cy = 602, 1118
    draw.rounded_rectangle(
        [y1_cx - 14, y1_cy - 10, y1_cx + 14, y1_cy + 10],
        radius=3,
        fill=SILVER_PAD,
        outline=(120, 125, 130, 255),
        width=1,
    )
    draw.text((y1_cx - 10, y1_cy - 26), "Y1", fill=TEAL, font=font_ref)

    # POWER BAT / USB Switch at bottom-left
    sw_cx, sw_cy = 356, 1368
    draw.text((sw_cx - 36, sw_cy - 48), "POWER", fill=TEAL, font=font_btn)
    draw.text((sw_cx - 88, sw_cy - 12), "BAT", fill=TEAL, font=font_btn)
    draw.text((sw_cx + 48, sw_cy - 12), "USB", fill=TEAL, font=font_btn)
    draw.rounded_rectangle(
        [sw_cx - 40, sw_cy - 18, sw_cx + 40, sw_cy + 18],
        radius=4,
        fill=SILVER_PAD,
        outline=(110, 115, 120, 255),
        width=2,
    )
    draw.rectangle([sw_cx - 28, sw_cy - 12, sw_cx + 28, sw_cy + 12], fill=(28, 30, 32, 255))
    # Slider knob toward USB
    draw.rectangle([sw_cx + 2, sw_cy - 10, sw_cx + 24, sw_cy + 10], fill=(70, 74, 78, 255))

    # Bottom 2x10 expansion header footprint (J4) framed in teal
    j4_x0, j4_y0, j4_x1, j4_y1 = 468, 1296, 816, 1412
    draw.rectangle([j4_x0, j4_y0, j4_x1, j4_y1], outline=TEAL, width=3)
    draw.text((j4_x0 + 8, j4_y1 - 20), "J4", fill=TEAL, font=font_ref)
    for col in range(10):
        px = j4_x0 + 38 + col * 30
        for row_idx, py in enumerate([j4_y0 + 36, j4_y0 + 70]):
            if col == 0 and row_idx == 1:
                draw.rectangle([px - 8, py - 8, px + 8, py + 8], fill=(40, 44, 48, 255), outline=SILVER_PAD, width=2)
            else:
                draw.ellipse([px - 8, py - 8, px + 8, py + 8], fill=(40, 44, 48, 255), outline=SILVER_PAD, width=2)

    # Bottom-right Micro-USB port
    usb_cx, usb_cy = 880, 1348
    draw.rounded_rectangle(
        [usb_cx - 38, usb_cy - 26, usb_cx + 38, usb_cy + 26],
        radius=4,
        fill=SILVER_PAD,
        outline=(120, 126, 130, 255),
        width=2,
    )
    draw.rectangle([usb_cx - 24, usb_cy - 12, usb_cx + 24, usb_cy + 12], fill=(55, 60, 64, 255))

    board_png_path = OUT_DIR / "board.png"
    img.save(board_png_path, "PNG")

    # Build layout.json
    leds_json = []
    for item in led_meta:
        cx, cy = led_positions[(item["category"], item["row"])]
        leds_json.append(
            {
                "index": item["index"],
                "ref": item["label"],
                "label": item["label"],
                "category": item["category"],
                "row": item["row"],
                "mask_bit": item["mask_bit"],
                "r": item["r"],
                "g": item["g"],
                "b": item["b"],
                "x_mm": round(cx / SCALE, 2),
                "y_mm": round(cy / SCALE, 2),
                "radius_mm": 1.15,
                "x_px": cx,
                "y_px": cy,
                "radius_px": 11.5,
            }
        )

    buttons_json = [
        {
            "id": "up",
            "name": "UP",
            "label": "UP (GP15)",
            "ref": "UP",
            "pin": "GP15",
            "gpio": 15,
            "bit": 1,
            "shape": "rect",
            "x_mm": round(up_center[0] / SCALE, 2),
            "y_mm": round(up_center[1] / SCALE, 2),
            "x_px": up_center[0],
            "y_px": up_center[1],
            "w_px": 72,
            "h_px": 72,
            "radius_px": 36,
            "min_hit_px": 44,
        },
        {
            "id": "down",
            "name": "DOWN",
            "label": "DOWN (GP16)",
            "ref": "DOWN",
            "pin": "GP16",
            "gpio": 16,
            "bit": 2,
            "shape": "rect",
            "x_mm": round(down_center[0] / SCALE, 2),
            "y_mm": round(down_center[1] / SCALE, 2),
            "x_px": down_center[0],
            "y_px": down_center[1],
            "w_px": 72,
            "h_px": 72,
            "radius_px": 36,
            "min_hit_px": 44,
        },
        {
            "id": "select",
            "name": "SELECT",
            "label": "SELECT (GP17)",
            "ref": "SELECT",
            "pin": "GP17",
            "gpio": 17,
            "bit": 16,
            "shape": "rect",
            "x_mm": round(select_center[0] / SCALE, 2),
            "y_mm": round(select_center[1] / SCALE, 2),
            "x_px": select_center[0],
            "y_px": select_center[1],
            "w_px": 72,
            "h_px": 72,
            "radius_px": 36,
            "min_hit_px": 44,
        },
    ]

    layout_data = {
        "board_image": {
            "file": "board.png",
            "width_px": W,
            "height_px": H,
            "scale_px_per_mm": SCALE,
            "origin_x_mm": 0.0,
            "origin_y_mm": 0.0,
            "source_note": "Drawn from a photo of a real 2022 badge, not from board files, so positions are approximate.",
        },
        "outline": {
            "bbox_mm": {"x": 2.6, "y": 4.6, "w": 114.8, "h": 137.0},
            "bbox_px": {"x": 26, "y": 46, "w": 1148, "h": 1370},
        },
        "buttons": buttons_json,
        "action_buttons": buttons_json,
        "leds": leds_json,
    }

    layout_path = OUT_DIR / "layout.json"
    layout_path.write_text(json.dumps(layout_data, indent=2) + "\n", encoding="utf-8")

    badge_data = {
        "year": 2022,
        "name": "Clue notepad badge",
        "name_is_description": True,
        "title": "SAINTCON 2022 Clue notepad badge",
        "mcu": "RP2040 Dual Cortex-M0+ (8 MHz emulated CPU / 1 MHz true hardware timer)",
        "core_badge_label": "RP2040 CORE",
        "has_screen": False,
        "screen_size": None,
        "has_touch": False,
        "has_2022_companion": True,
        "led_count": 52,
        "led_label": "52 Charlieplexed RGB LEDs (GP0..GP14, 156 channels)",
        "led_summary": "52 RGB LEDs (21 Suspects, 21 Attacks/Exploits, 10 Rooms)",
        "eeprom_bytes": 8192,
        "has_nfc": False,
        "has_speed_control": False,
        "buttons": [
            {
                "id": "up",
                "name": "UP",
                "bit": 1,
                "gpio": 15,
                "keys": ["ArrowUp", "KeyW", "w", "W"],
                "hint": "↑ / W",
                "ref": "GP15",
                "kind": "dpad_up",
            },
            {
                "id": "down",
                "name": "DOWN",
                "bit": 2,
                "gpio": 16,
                "keys": ["ArrowDown", "KeyS", "s", "S"],
                "hint": "↓ / S",
                "ref": "GP16",
                "kind": "dpad_down",
            },
            {
                "id": "select",
                "name": "SELECT",
                "bit": 16,
                "gpio": 17,
                "keys": ["Enter", "Space", " ", "KeyE", "e", "E"],
                "hint": "Enter / Space",
                "ref": "GP17",
                "kind": "dpad_center",
            },
        ],
        "files": {
            "layout": "layout.json",
            "board": "board.png",
            "core_js": "core.js",
            "uf2": "Saintcon2022BadgeFirmwareFix.uf2",
        },
        "default_mode": "stock",
        "modes_available": ["stock"],
        "modes_coming_soon": [],
        "modes": {
            "stock": {
                "id": "stock",
                "name": "Original Firmware",
                "label": "Original Firmware",
                "status": "available",
                "core_js": "core.js",
                "uf2": "Saintcon2022BadgeFirmwareFix.uf2",
                "has_speed_control": False,
                "has_2022_companion": True,
                "description": "Original SAINTCON 2022 RP2040 firmware (Saintcon2022BadgeFirmwareFix.uf2)",
            }
        },
        "credits": {
            "badge_by": "compukidmike",
            "repo_only": True,
            "author_url": "https://github.com/compukidmike",
            "repo_url": "https://github.com/compukidmike/saintcon2022",
            "source_url": "https://github.com/compukidmike/saintcon2022/blob/383964e2e0e576dd3c153fc479d37fb429ce2969/Saintcon2022BadgeFirmwareFix.uf2",
            "commit": "383964e",
            "license": "Licence not stated",
            "third_party_licenses_url": "badges/2022/THIRD_PARTY_LICENSES.txt",
            "other_credits": [],
        },
    }

    badge_path = OUT_DIR / "badge.json"
    badge_path.write_text(json.dumps(badge_data, indent=2) + "\n", encoding="utf-8")
    print(f"[draw_layout_2022] Wrote {board_png_path} ({W}x{H}), {layout_path}, and {badge_path}")


if __name__ == "__main__":
    main()
