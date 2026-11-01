#!/usr/bin/env python3
"""
Parse SAINTCON KiCad .kicad_pcb files (S-expression) to extract exact board
geometry, trace the WS2812 LED data chain from the MCU GPIO pin through all LEDs
by net name, produce a tinted KiCad board render at
emulator/web/badges/<year>/board.png, write
emulator/web/badges/<year>/layout.json, and draw
emulator/build/check-overlay-<year>.png.

Supports:
  --year 2018  (Saintcon2018.kicad_pcb — 8x32 LED matrix badge)
  --year 2023  (Saintcon2023.kicad_pcb — "Incidents" Dungeon RPG)
  --year 2024  (Saintcon2024MainBoard + Saintcon2024TopBoard — "Wrist Communicator" / Tower Battle)
  --year 2025  (Saintcon2025.kicad_pcb — "The Wrench")
  --year 2026  (Apollo.kicad_pcb       — "Orion Trail" / Wayfarer)
  --year all   (all supported years)
"""

import argparse
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import re
import subprocess
import zipfile

from PIL import Image, ImageDraw
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "emulator" / "build"
BADGES_DIR = ROOT / "emulator" / "web" / "badges"

# Legacy 2025 paths kept as module attributes for callers/importers
ZIP_PATH = (
    ROOT
    / "upstream"
    / "Saintcon2025"
    / "Hardware"
    / "Badge"
    / "Saintcon2025KiCadFiles.zip"
)
KICAD_DIR = BUILD_DIR / "kicad"
PCB_PATH = KICAD_DIR / "Saintcon2025.kicad_pcb"


def find_2018_pcb_default() -> Path:
  candidates = [
      ROOT
      / "upstream"
      / "Saintcon2018"
      / "Badge"
      / "Hardware Files"
      / "KiCad Files"
      / "Saintcon2018.kicad_pcb",
      Path.home()
      / "Projects"
      / "saintcon-museum-research"
      / "Saintcon2018"
      / "Badge"
      / "Hardware Files"
      / "KiCad Files"
      / "Saintcon2018.kicad_pcb",
  ]
  for c in candidates:
    if c.exists():
      return c
  return candidates[0]


def find_2023_kicad_zip_default() -> Path:
  candidates = [
      ROOT / "upstream" / "Saintcon2023" / "Hardware" / "Saintcon2023KiCad.zip",
      Path.home() / "Projects" / "saintcon-museum-research" / "Saintcon2023" / "Hardware" / "Saintcon2023KiCad.zip",
  ]
  for c in candidates:
    if c.exists():
      return c
  return candidates[0]


def find_2024_kicad_zips_default():
  roots = [
      ROOT / "upstream" / "Saintcon2024" / "Hardware",
      Path.home() / "Projects" / "saintcon-museum-research" / "Saintcon2024" / "Hardware",
  ]
  for r in roots:
    main_zip = r / "Saintcon2024MainBoard" / "Saintcon2024-KiCadProject.zip"
    top_zip = r / "Saintcon2024TopBoard" / "Saintcon2024Top-KiCadProject.zip"
    if main_zip.exists() and top_zip.exists():
      return main_zip, top_zip
  return (
      roots[0] / "Saintcon2024MainBoard" / "Saintcon2024-KiCadProject.zip",
      roots[0] / "Saintcon2024TopBoard" / "Saintcon2024Top-KiCadProject.zip",
  )


def find_2026_pcb_default() -> Path:
  candidates = [
      ROOT
      / "upstream"
      / "SAINTCON-BADGE-2026"
      / "hardware"
      / "Apollo.kicad_pcb",
      Path.home()
      / "Projects"
      / "saintcon-museum-research"
      / "SAINTCON-BADGE-2026"
      / "hardware"
      / "Apollo.kicad_pcb",
  ]
  for c in candidates:
    if c.exists():
      return c
  return candidates[0]


def parse_sexp(text: str):
  """Minimal fast S-expression parser for KiCad .kicad_pcb files."""
  tokens = []
  i = 0
  n = len(text)
  while i < n:
    c = text[i]
    if c in " \t\r\n":
      i += 1
    elif c in "()":
      tokens.append(c)
      i += 1
    elif c == '"':
      j = i + 1
      while j < n:
        if text[j] == "\\":
          j += 2
        elif text[j] == '"':
          break
        else:
          j += 1
      tokens.append(text[i + 1 : j])
      i = j + 1
    else:
      j = i
      while j < n and text[j] not in " \t\r\n()":
        j += 1
      tokens.append(text[i:j])
      i = j

  stack = [[]]
  for tok in tokens:
    if tok == "(":
      lst = []
      stack[-1].append(lst)
      stack.append(lst)
    elif tok == ")":
      stack.pop()
    else:
      stack[-1].append(tok)
  return stack[0][0]


def kicad_local_to_board(
    fx: float, fy: float, rot_deg: float, layer: str, lx: float, ly: float
):
  """Transform footprint-local (lx, ly) to board (bx, by) in mm."""
  if layer.startswith("B."):
    lx = -lx
  theta = math.radians(rot_deg)
  cos_t = math.cos(theta)
  sin_t = math.sin(theta)
  bx = fx + lx * cos_t + ly * sin_t
  by = fy - lx * sin_t + ly * cos_t
  return bx, by


def downgrade_kicad10_to_kicad9(pcb_text: str) -> str:
  """Convert KiCad 10 (version 2026xxxx) PCB syntax to KiCad 9 for kicad-cli 9.0."""
  if "(version 2026" not in pcb_text[:200]:
    return pcb_text

  net_names = re.findall(r'\(net\s+"([^"]*)"\)', pcb_text)
  net_ids = {"": 0}
  for name in net_names:
    if name not in net_ids:
      net_ids[name] = len(net_ids)

  txt9 = re.sub(r"\(version\s+2026\d+\)", "(version 20241229)", pcb_text, count=1)
  txt9 = txt9.replace('(generator_version "10.0")', '(generator_version "9.0")', 1)
  txt9 = re.sub(r"\s*\(tenting\s+\(front\s+\w+\)\s+\(back\s+\w+\)\s*\)", "", txt9)
  txt9 = re.sub(r"\s*\(covering\s+\(front\s+\w+\)\s+\(back\s+\w+\)\s*\)", "", txt9)
  txt9 = re.sub(r"\s*\(plugging\s+\(front\s+\w+\)\s+\(back\s+\w+\)\s*\)", "", txt9)
  txt9 = re.sub(r"\s*\(capping\s+\w+\)", "", txt9)
  txt9 = re.sub(r"\s*\(filling\s+\w+\)", "", txt9)
  txt9 = re.sub(r"\s*\(duplicate_pad_numbers_are_jumpers\s+\w+\)", "", txt9)
  txt9 = re.sub(
      r"\s*\(units\s+(?:\(unit\b[^()]*?(?:\([^()]*\)[^()]*?)*\)\s*)+\)",
      "",
      txt9,
  )
  txt9 = re.sub(
      r'\s*\(placement\s+\(enabled\s+\w+\)\s+\(sheetname\s+"[^"]*"\)\s*\)',
      "",
      txt9,
  )
  txt9 = re.sub(r"\s*\(radius\s+[0-9.]+\)", "", txt9)
  txt9 = re.sub(r"\s*\(island\s+\w+\)", "", txt9)

  net_table = (
      "\n".join(
          f'\t(net {nid} "{name}")'
          for name, nid in sorted(net_ids.items(), key=lambda kv: kv[1])
      )
      + "\n"
  )
  first_fp = txt9.find("\t(footprint ")
  if first_fp != -1:
    txt9 = txt9[:first_fp] + net_table + txt9[first_fp:]

  txt9 = re.sub(
      r'(\(zone\s+)\(net\s+"([^"]*)"\)',
      lambda m: (
          f'{m.group(1)}(net {net_ids[m.group(2)]})\n\t\t(net_name'
          f' "{m.group(2)}")'
      ),
      txt9,
  )
  txt9 = re.sub(
      r'(\((?:segment|via|arc)\b[\s\S]*?)\(net\s+"([^"]*)"\)',
      lambda m: f"{m.group(1)}(net {net_ids[m.group(2)]})",
      txt9,
  )
  txt9 = re.sub(
      r'\(net\s+"([^"]*)"\)',
      lambda m: f'(net {net_ids[m.group(1)]} "{m.group(1)}")',
      txt9,
  )
  return txt9


def ensure_kicad_extracted():
  KICAD_DIR.mkdir(parents=True, exist_ok=True)
  if not PCB_PATH.exists():
    with zipfile.ZipFile(ZIP_PATH, "r") as zf:
      zf.extractall(KICAD_DIR)


def parse_footprints_and_graphics(tree):
  """Parse footprints (handling both KiCad 9 and KiCad 10 pad nets) and top-level graphics."""
  footprints = {}
  edge_segments = []
  top_polys = []
  top_circles = []

  for node in tree:
    if not isinstance(node, list) or not node:
      continue
    tag0 = node[0]
    if tag0 == "gr_poly":
      p_layer = None
      pts = []
      for sub in node[1:]:
        if isinstance(sub, list) and sub:
          if sub[0] == "layer":
            p_layer = sub[1]
          elif sub[0] == "pts":
            for xy in sub[1:]:
              if isinstance(xy, list) and xy[0] == "xy":
                pts.append((float(xy[1]), float(xy[2])))
      top_polys.append({"layer": p_layer, "pts": pts})
    elif tag0 == "gr_circle":
      c_layer = None
      cx = cy = ex = ey = 0.0
      for sub in node[1:]:
        if isinstance(sub, list) and sub:
          if sub[0] == "layer":
            c_layer = sub[1]
          elif sub[0] == "center":
            cx, cy = float(sub[1]), float(sub[2])
          elif sub[0] == "end":
            ex, ey = float(sub[1]), float(sub[2])
      top_circles.append({
          "layer": c_layer,
          "center": (cx, cy),
          "radius": math.hypot(ex - cx, ey - cy),
      })
    elif tag0 in ("footprint", "module"):
      fp_lib = node[1]
      layer = "F.Cu"
      fx = fy = rot = 0.0
      ref = None
      val = None
      pads = []
      fp_lines = []
      fp_rects = []
      fp_polys = []

      for sub in node[2:]:
        if not isinstance(sub, list) or not sub:
          continue
        tag = sub[0]
        if tag == "layer":
          layer = sub[1]
        elif tag == "at":
          fx = float(sub[1])
          fy = float(sub[2])
          rot = float(sub[3]) if len(sub) > 3 else 0.0
        elif tag == "property" and len(sub) >= 3:
          if sub[1] == "Reference":
            ref = sub[2]
          elif sub[1] == "Value":
            val = sub[2]
        elif tag == "fp_text" and len(sub) >= 3 and sub[1] == "reference":
          if ref is None:
            ref = sub[2]
        elif tag == "fp_text" and len(sub) >= 3 and sub[1] == "value":
          if val is None:
            val = sub[2]
        elif tag == "pad":
          pad_num = sub[1]
          pad_type = sub[2] if len(sub) > 2 else ""
          px = py = prot = 0.0
          pw = ph = 0.0
          net_id = None
          net_name = None
          pinfunc = None
          for psub in sub[2:]:
            if not isinstance(psub, list) or not psub:
              continue
            if psub[0] == "at":
              px = float(psub[1])
              py = float(psub[2])
              prot = float(psub[3]) if len(psub) > 3 else 0.0
            elif psub[0] == "size":
              pw = float(psub[1])
              ph = float(psub[2])
            elif psub[0] == "net":
              if len(psub) >= 3:
                net_id = int(psub[1]) if str(psub[1]).isdigit() else None
                net_name = psub[2]
              elif len(psub) == 2:
                if str(psub[1]).isdigit():
                  net_id = int(psub[1])
                  net_name = ""
                else:
                  net_name = psub[1]
            elif psub[0] == "pinfunction":
              pinfunc = psub[1]
          pads.append({
              "num": pad_num,
              "type": pad_type,
              "lx": px,
              "ly": py,
              "rot": prot,
              "w": pw,
              "h": ph,
              "net_id": net_id,
              "net_name": net_name,
              "pinfunction": pinfunc,
          })
        elif tag == "fp_line":
          l_layer = None
          sx = sy = ex = ey = 0.0
          for lsub in sub[1:]:
            if isinstance(lsub, list) and lsub:
              if lsub[0] == "layer":
                l_layer = lsub[1]
              elif lsub[0] == "start":
                sx, sy = float(lsub[1]), float(lsub[2])
              elif lsub[0] == "end":
                ex, ey = float(lsub[1]), float(lsub[2])
          bsx, bsy = kicad_local_to_board(fx, fy, rot, layer, sx, sy)
          bex, bey = kicad_local_to_board(fx, fy, rot, layer, ex, ey)
          fp_lines.append({
              "layer": l_layer,
              "start": (bsx, bsy),
              "end": (bex, bey),
              "local_start": (sx, sy),
              "local_end": (ex, ey),
          })
          if l_layer == "Edge.Cuts" and "Artwork" in fp_lib:
            edge_segments.append(
                ((round(bsx, 4), round(bsy, 4)), (round(bex, 4), round(bey, 4)))
            )
        elif tag == "fp_rect":
          r_layer = None
          sx = sy = ex = ey = 0.0
          for rsub in sub[1:]:
            if isinstance(rsub, list) and rsub:
              if rsub[0] == "layer":
                r_layer = rsub[1]
              elif rsub[0] == "start":
                sx, sy = float(rsub[1]), float(rsub[2])
              elif rsub[0] == "end":
                ex, ey = float(rsub[1]), float(rsub[2])
          bsx, bsy = kicad_local_to_board(fx, fy, rot, layer, sx, sy)
          bex, bey = kicad_local_to_board(fx, fy, rot, layer, ex, ey)
          fp_rects.append({
              "layer": r_layer,
              "x1": min(bsx, bex),
              "y1": min(bsy, bey),
              "x2": max(bsx, bex),
              "y2": max(bsy, bey),
          })
        elif tag == "fp_poly":
          p_layer = None
          pts = []
          for psub in sub[1:]:
            if isinstance(psub, list) and psub:
              if psub[0] == "layer":
                p_layer = psub[1]
              elif psub[0] == "pts":
                for xy in psub[1:]:
                  if isinstance(xy, list) and xy[0] == "xy":
                    bx, by = kicad_local_to_board(
                        fx, fy, rot, layer, float(xy[1]), float(xy[2])
                    )
                    pts.append((bx, by))
          fp_polys.append({"layer": p_layer, "pts": pts})

      fp_info = {
          "ref": ref,
          "lib": fp_lib,
          "val": val,
          "layer": layer,
          "x": fx,
          "y": fy,
          "rot": rot,
          "pads": pads,
          "lines": fp_lines,
          "rects": fp_rects,
          "polys": fp_polys,
      }
      key = ref if ref and ref not in footprints else f"{ref}_{fp_lib}"
      footprints[key] = fp_info

  return footprints, edge_segments, top_polys, top_circles


def trace_ws2812_chain(footprints, mcu_ref: str, gpio_num: int, expected_count: int):
  """Trace WS2812B/WS2812C chain from MCU GPIO pad through DIN -> DOUT nets."""
  mcu = footprints[mcu_ref]
  gpio_token = f"GPIO{gpio_num}"
  start_net = None
  for pad in mcu["pads"]:
    pf = pad["pinfunction"] or ""
    if re.search(rf"(^|[_/]){gpio_token}($|[_/])", pf):
      start_net = pad["net_name"]
      break
  if not start_net:
    raise RuntimeError(f"Could not find {gpio_token} pad on {mcu_ref}")

  passive_bridges = defaultdict(list)
  for ref, fp in footprints.items():
    if ref.startswith(("R", "JP")) and len(fp["pads"]) == 2:
      n0 = fp["pads"][0]["net_name"]
      n1 = fp["pads"][1]["net_name"]
      if n0 and n1 and n0 not in ("GND", "+3V3", "+3.3V") and n1 not in ("GND", "+3V3", "+3.3V"):
        passive_bridges[n0].append((ref, n1))
        passive_bridges[n1].append((ref, n0))

  din_to_led = {}
  for ref, fp in footprints.items():
    val = fp["val"] or ""
    if any(k in val or k in fp["lib"] for k in ("WS2812", "SK6812")):
      din_net = None
      dout_net = None
      for pad in fp["pads"]:
        pf = pad["pinfunction"] or ""
        if pf == "DIN" or pf.startswith("DIN_"):
          din_net = pad["net_name"]
        elif pf == "DOUT" or pf.startswith("DOUT_"):
          dout_net = pad["net_name"]
      if din_net:
        din_to_led[din_net] = (ref, fp, din_net, dout_net)

  led_chain = []
  curr_net = start_net
  visited_nets = set()
  while curr_net and curr_net not in visited_nets:
    visited_nets.add(curr_net)
    if curr_net not in din_to_led:
      bridged = None
      for _rref, next_net in passive_bridges.get(curr_net, []):
        if next_net in din_to_led and next_net not in visited_nets:
          bridged = next_net
          break
      if bridged:
        curr_net = bridged
      else:
        break
    if curr_net in din_to_led:
      ref, fp, din_net, dout_net = din_to_led[curr_net]
      led_chain.append({
          "index": len(led_chain),
          "ref": ref,
          "x_mm": round(fp["x"], 4),
          "y_mm": round(fp["y"], 4),
          "rot_deg": round(fp["rot"], 2),
          "din_net": din_net,
          "dout_net": dout_net,
      })
      curr_net = dout_net

  if len(led_chain) != expected_count:
    raise RuntimeError(
        f"Expected {expected_count} LEDs in chain from {mcu_ref} {gpio_token}, found {len(led_chain)}"
    )
  return start_net, led_chain


# ============================================================================
# 2025 ("The Wrench") extraction & rendering
# ============================================================================

def extract_geometry_2025(tree):
  footprints, edge_segments, _top_polys, _top_circles = parse_footprints_and_graphics(tree)

  adj = defaultdict(list)
  for idx, (s, e) in enumerate(edge_segments):
    adj[s].append((idx, e))
    adj[e].append((idx, s))

  used = set()
  loops = []
  for idx in range(len(edge_segments)):
    if idx in used:
      continue
    s, e = edge_segments[idx]
    used.add(idx)
    loop = [s, e]
    curr = e
    while True:
      found = False
      for nidx, nxt in adj[curr]:
        if nidx not in used:
          used.add(nidx)
          loop.append(nxt)
          curr = nxt
          found = True
          break
      if not found:
        break
    if len(loop) > 1 and loop[0] == loop[-1]:
      loop = loop[:-1]
    loops.append(loop)

  loops.sort(key=len, reverse=True)
  outer_loop = loops[0]
  hex_cutout_loop = loops[1] if len(loops) > 1 else []

  gpio3_net, led_chain = trace_ws2812_chain(footprints, "U4", 3, 20)

  u2 = footprints["U2"]
  f_silks_rects = [r for r in u2["rects"] if r["layer"] == "F.SilkS"]
  if not f_silks_rects:
    raise RuntimeError("Could not find F.SilkS rect on U2")
  mod_rect = f_silks_rects[0]
  mod_x = mod_rect["x1"]
  mod_y = mod_rect["y1"]
  mod_w = mod_rect["x2"] - mod_rect["x1"]
  mod_h = mod_rect["y2"] - mod_rect["y1"]

  act_w = 30.60
  act_h = 40.80
  act_x = mod_x + (mod_w - act_w) / 2.0
  act_y = mod_y + 2.20

  sw2 = footprints["SW2"]
  sw2_info = {
      "ref": "SW2",
      "x_mm": round(sw2["x"], 4),
      "y_mm": round(sw2["y"], 4),
      "rot_deg": round(sw2["rot"], 2),
      "radius_mm": round(6.2 * math.sqrt(2.0), 4),
  }

  sw1 = footprints["SW1"]
  sw1_xs = [p[0] for l in sw1["lines"] if l["layer"] == "B.SilkS" for p in (l["start"], l["end"])]
  sw1_ys = [p[1] for l in sw1["lines"] if l["layer"] == "B.SilkS" for p in (l["start"], l["end"])]
  sw1_info = {
      "ref": "SW1",
      "x_mm": round(sw1["x"], 4),
      "y_mm": round(sw1["y"], 4),
      "rot_deg": round(sw1["rot"], 2),
      "w_mm": round(max(sw1_xs) - min(sw1_xs), 4),
      "h_mm": round(max(sw1_ys) - min(sw1_ys), 4),
  }

  minibadges = []
  for mb_ref in ("U10", "U11", "U13"):
    mb = footprints[mb_ref]
    silks_pts = [p for l in mb["lines"] if l["layer"] == "F.SilkS" for p in (l["start"], l["end"])]
    mxs = [p[0] for p in silks_pts]
    mys = [p[1] for p in silks_pts]
    cx = (min(mxs) + max(mxs)) / 2.0
    cy = (min(mys) + max(mys)) / 2.0
    minibadges.append({
        "ref": mb_ref,
        "x_mm": round(cx, 4),
        "y_mm": round(cy, 4),
        "w_mm": round(max(mxs) - min(mxs), 4),
        "h_mm": round(max(mys) - min(mys), 4),
        "origin_x_mm": round(mb["x"], 4),
        "origin_y_mm": round(mb["y"], 4),
        "rot_deg": round(mb["rot"], 2),
    })
  minibadges.sort(key=lambda m: m["y_mm"])
  for idx, mb in enumerate(minibadges):
    mb["slot"] = idx

  return {
      "outer_loop": outer_loop,
      "hex_cutout_loop": hex_cutout_loop,
      "led_chain": led_chain,
      "gpio3_net": gpio3_net,
      "screen_active_mm": (act_x, act_y, act_w, act_h),
      "screen_module_mm": (mod_x, mod_y, mod_w, mod_h),
      "sw2": sw2_info,
      "sw1": sw1_info,
      "minibadges": minibadges,
  }


def align_svg_layers(layer_dir: Path, names, w_full: int, h_full: int, x0_mm: float, y0_mm: float, w_mm: float, h_mm: float):
  for name in names:
    aligned_png = layer_dir / f"{name}-aligned.png"
    svg_text = (layer_dir / f"{name}.svg").read_text(encoding="utf-8")
    svg_text = re.sub(
        r'width="[^"]+"\s+height="[^"]+"\s+viewBox="[^"]+"',
        f'width="{w_full}px" height="{h_full}px" viewBox="{x0_mm:.6f} {y0_mm:.6f} {w_mm:.6f} {h_mm:.6f}"',
        svg_text,
        count=1,
    )
    aligned_svg = layer_dir / f"{name}-aligned.svg"
    aligned_svg.write_text(svg_text, encoding="utf-8")
    subprocess.run(
        [
            "rsvg-convert",
            "-w",
            str(w_full),
            "-h",
            str(h_full),
            str(aligned_svg),
            "-o",
            str(aligned_png),
        ],
        check=True,
    )


def ensure_kicad_renders_2025(outer_loop, out_board_png: Path):
  render_3d_basic = BUILD_DIR / "render-test3.png"
  fmask_svg = BUILD_DIR / "layer-fmask.svg"
  fsilks_sub_svg = BUILD_DIR / "layer-fsilks-sub.svg"
  fcu_svg = BUILD_DIR / "layer-fcu.svg"

  if not (
      render_3d_basic.exists()
      and fmask_svg.exists()
      and fsilks_sub_svg.exists()
      and fcu_svg.exists()
  ):
    pcb_text = PCB_PATH.read_text(encoding="utf-8", errors="replace")
    pcb_mod = (
        pcb_text.replace(
            "(layerselection 0x00000000_00000000_55555555_57555555)",
            "(layerselection 0x00000000_00000000_00000000_0200a0af)",
        )
        .replace("(subtractmaskfromsilk no)", "(subtractmaskfromsilk yes)")
        .replace('(color "FR4 natural")', '(color "#B8892DFF")')
        .replace('(color "#848484FF")', '(color "#9EA4AAFF")')
        .replace('(copper_finish "HAL lead-free")', '(copper_finish "ENIG")')
    )
    temp_pcb = KICAD_DIR / "render_temp.kicad_pcb"
    temp_pcb.write_text(pcb_mod, encoding="utf-8")
    uid_gid = f"{os.getuid()}:{os.getgid()}"
    cmd = (
        "kicad-cli pcb render --side top --background transparent --quality"
        " basic -w 1600 -h 3600 -o emulator/build/render-test3.png"
        " emulator/build/kicad/render_temp.kicad_pcb && kicad-cli pcb render"
        " --side top --background transparent --quality basic -w 1600 -h 3600"
        " -o emulator/build/render-3d.png"
        " emulator/build/kicad/Saintcon2025.kicad_pcb && kicad-cli pcb export"
        " svg --layers F.Mask --black-and-white --page-size-mode 0"
        " --exclude-drawing-sheet -o emulator/build/layer-fmask.svg"
        " emulator/build/kicad/Saintcon2025.kicad_pcb && kicad-cli pcb export"
        " svg --layers F.SilkS --subtract-soldermask --black-and-white"
        " --page-size-mode 0 --exclude-drawing-sheet -o"
        " emulator/build/layer-fsilks-sub.svg"
        " emulator/build/kicad/Saintcon2025.kicad_pcb && kicad-cli pcb export"
        " svg --layers F.Cu --black-and-white --page-size-mode 0"
        " --exclude-drawing-sheet -o emulator/build/layer-fcu.svg"
        " emulator/build/kicad/Saintcon2025.kicad_pcb"
    )
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-u",
            uid_gid,
            "-v",
            f"{ROOT}:/w",
            "-w",
            "/w",
            "kicad/kicad:9.0",
            "sh",
            "-c",
            cmd,
        ],
        check=True,
    )

  im3d = Image.open(render_3d_basic).convert("RGBA")
  W_full, H_full = im3d.size
  arr3d = np.array(im3d, dtype=np.float32)
  ys, xs = np.where(arr3d[:, :, 3] > 0)
  xmin_px_full, xmax_px_full = int(xs.min()), int(xs.max())
  ymin_px_full, ymax_px_full = int(ys.min()), int(ys.max())

  oxs = [p[0] for p in outer_loop]
  oys = [p[1] for p in outer_loop]
  xmin_mm, xmax_mm = min(oxs), max(oxs)
  ymin_mm, ymax_mm = min(oys), max(oys)

  scale_full = (
      (xmax_px_full - xmin_px_full) / (xmax_mm - xmin_mm)
      + (ymax_px_full - ymin_px_full) / (ymax_mm - ymin_mm)
  ) / 2.0
  x0_mm = xmin_mm - xmin_px_full / scale_full
  y0_mm = ymin_mm - ymin_px_full / scale_full
  w_mm = W_full / scale_full
  h_mm = H_full / scale_full

  align_svg_layers(
      BUILD_DIR,
      ("layer-fmask", "layer-fsilks-sub", "layer-fcu"),
      W_full,
      H_full,
      x0_mm,
      y0_mm,
      w_mm,
      h_mm,
  )

  board_alpha = arr3d[:, :, 3] / 255.0
  fmask = np.array(Image.open(BUILD_DIR / "layer-fmask-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fsilks = np.array(Image.open(BUILD_DIR / "layer-fsilks-sub-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fcu = np.array(Image.open(BUILD_DIR / "layer-fcu-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0

  lum3d = (0.299 * arr3d[:, :, 0] + 0.587 * arr3d[:, :, 1] + 0.114 * arr3d[:, :, 2]) / 255.0
  out = np.zeros((H_full, W_full, 4), dtype=np.float32)

  base_r, base_g, base_b = 148.0, 154.0, 160.0
  trace_relief = fcu * (1.0 - fmask) * 10.0
  out[:, :, 0] = base_r + trace_relief + (lum3d - 0.85) * 25.0
  out[:, :, 1] = base_g + trace_relief + (lum3d - 0.85) * 25.0
  out[:, :, 2] = base_b + trace_relief + (lum3d - 0.85) * 25.0

  yy, xx = np.mgrid[0:H_full, 0:W_full]
  tex = np.sin(xx * 0.07 + yy * 0.05) * 4.0 + np.cos(xx * 0.03 - yy * 0.09) * 4.0
  gold_r = 188.0 + tex + (lum3d - 0.5) * 20.0
  gold_g = 144.0 + tex * 0.8 + (lum3d - 0.5) * 16.0
  gold_b = 52.0 + tex * 0.4 + (lum3d - 0.5) * 10.0

  out[:, :, 0] = out[:, :, 0] * (1.0 - fmask) + (gold_r * (1.0 - fcu * 0.55) + 195.0 * (fcu * 0.55)) * fmask
  out[:, :, 1] = out[:, :, 1] * (1.0 - fmask) + (gold_g * (1.0 - fcu * 0.55) + 198.0 * (fcu * 0.55)) * fmask
  out[:, :, 2] = out[:, :, 2] * (1.0 - fmask) + (gold_b * (1.0 - fcu * 0.55) + 202.0 * (fcu * 0.55)) * fmask

  silk_r, silk_g, silk_b = 22.0, 24.0, 26.0
  out[:, :, 0] = out[:, :, 0] * (1.0 - fsilks) + silk_r * fsilks
  out[:, :, 1] = out[:, :, 1] * (1.0 - fsilks) + silk_g * fsilks
  out[:, :, 2] = out[:, :, 2] * (1.0 - fsilks) + silk_b * fsilks

  out[:, :, 3] = board_alpha * 255.0
  res_full = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), mode="RGBA")

  W_out = W_full // 2
  H_out = H_full // 2
  scale_out = scale_full / 2.0
  res_out = res_full.resize((W_out, H_out), Image.Resampling.LANCZOS)

  draw = ImageDraw.Draw(res_out)

  def mm_to_px(xmm, ymm):
    return (xmm - x0_mm) * scale_out, (ymm - y0_mm) * scale_out

  mx1, my1 = mm_to_px(382.7, 63.7625)
  mx2, my2 = mm_to_px(417.3, 111.5625)
  draw.rectangle([mx1, my1, mx2, my2], fill=(18, 20, 24, 255), outline=(170, 176, 182, 255), width=2)
  ax1, ay1 = mm_to_px(384.7, 65.9625)
  ax2, ay2 = mm_to_px(415.3, 106.7625)
  draw.rectangle([ax1, ay1, ax2, ay2], fill=(8, 10, 14, 255), outline=(38, 42, 48, 255), width=1)

  sx1, sy1 = mm_to_px(417.0, 117.2)
  sx2, sy2 = mm_to_px(418.9, 119.5)
  draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=2, fill=(28, 30, 34, 255), outline=(60, 64, 70, 255))

  out_board_png.parent.mkdir(parents=True, exist_ok=True)
  res_out.save(out_board_png, optimize=True)

  return {
      "width_px": W_out,
      "height_px": H_out,
      "scale_px_per_mm": round(scale_out, 6),
      "origin_x_mm": round(x0_mm, 6),
      "origin_y_mm": round(y0_mm, 6),
  }, res_out


def build_2025():
  ensure_kicad_extracted()
  out_dir = BADGES_DIR / "2025"
  layout_path = out_dir / "layout.json"
  board_png_path = out_dir / "board.png"
  overlay_path = BUILD_DIR / "check-overlay.png"

  pcb_text = PCB_PATH.read_text(encoding="utf-8", errors="replace")
  tree = parse_sexp(pcb_text)
  geom = extract_geometry_2025(tree)
  img_meta, board_img = ensure_kicad_renders_2025(geom["outer_loop"], board_png_path)

  scale = img_meta["scale_px_per_mm"]
  x0 = img_meta["origin_x_mm"]
  y0 = img_meta["origin_y_mm"]

  def to_px(xmm, ymm):
    return round((xmm - x0) * scale, 2), round((ymm - y0) * scale, 2)

  outer_mm = [[round(x, 4), round(y, 4)] for x, y in geom["outer_loop"]]
  outer_px = [list(to_px(x, y)) for x, y in geom["outer_loop"]]
  hex_mm = [[round(x, 4), round(y, 4)] for x, y in geom["hex_cutout_loop"]]
  hex_px = [list(to_px(x, y)) for x, y in geom["hex_cutout_loop"]]

  oxs = [p[0] for p in outer_mm]
  oys = [p[1] for p in outer_mm]
  pxs = [p[0] for p in outer_px]
  pys = [p[1] for p in outer_px]

  act_x, act_y, act_w, act_h = geom["screen_active_mm"]
  act_px_x, act_px_y = to_px(act_x, act_y)
  act_px_w = round(act_w * scale, 2)
  act_px_h = round(act_h * scale, 2)

  mod_x, mod_y, mod_w, mod_h = geom["screen_module_mm"]
  mod_px_x, mod_px_y = to_px(mod_x, mod_y)
  mod_px_w = round(mod_w * scale, 2)
  mod_px_h = round(mod_h * scale, 2)

  screen_obj = {
      "ref": "U2",
      "part": "LCD-2_0-ST7789V-22PIN",
      "shape": "rect",
      "width_dots": 240,
      "height_dots": 320,
      "x_mm": round(act_x, 4),
      "y_mm": round(act_y, 4),
      "w_mm": round(act_w, 4),
      "h_mm": round(act_h, 4),
      "x_px": act_px_x,
      "y_px": act_px_y,
      "w_px": act_px_w,
      "h_px": act_px_h,
      "module_mm": {"x": round(mod_x, 4), "y": round(mod_y, 4), "w": round(mod_w, 4), "h": round(mod_h, 4)},
      "module_px": {"x": mod_px_x, "y": mod_px_y, "w": mod_px_w, "h": mod_px_h},
  }

  sw2 = geom["sw2"]
  sw2["x_px"], sw2["y_px"] = to_px(sw2["x_mm"], sw2["y_mm"])
  sw2["radius_px"] = round(sw2["radius_mm"] * scale, 2)

  sw1 = geom["sw1"]
  sw1["x_px"], sw1["y_px"] = to_px(sw1["x_mm"], sw1["y_mm"])
  sw1["w_px"] = round(sw1["w_mm"] * scale, 2)
  sw1["h_px"] = round(sw1["h_mm"] * scale, 2)

  for mb in geom["minibadges"]:
    mb["x_px"], mb["y_px"] = to_px(mb["x_mm"], mb["y_mm"])
    mb["w_px"] = round(mb["w_mm"] * scale, 2)
    mb["h_px"] = round(mb["h_mm"] * scale, 2)

  for led in geom["led_chain"]:
    led["x_px"], led["y_px"] = to_px(led["x_mm"], led["y_mm"])

  layout = {
      "board_image": {"file": "board.png", **img_meta},
      "outline": {
          "bbox_mm": {"x": round(min(oxs), 4), "y": round(min(oys), 4), "w": round(max(oxs) - min(oxs), 4), "h": round(max(oys) - min(oys), 4)},
          "bbox_px": {"x": round(min(pxs), 2), "y": round(min(pys), 2), "w": round(max(pxs) - min(pxs), 2), "h": round(max(pys) - min(pys), 2)},
          "polygon_mm": outer_mm,
          "polygon_px": outer_px,
          "hex_hole_mm": hex_mm,
          "hex_hole_px": hex_px,
      },
      "screen": screen_obj,
      "sw2": sw2,
      "sw1": sw1,
      "minibadges": geom["minibadges"],
      "led_chain_source": {
          "mcu_ref": "U4",
          "gpio": 3,
          "start_net": geom["gpio3_net"],
          "order_refs": [l["ref"] for l in geom["led_chain"]],
      },
      "leds": geom["led_chain"],
  }

  out_dir.mkdir(parents=True, exist_ok=True)
  layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

  overlay = board_img.copy()
  draw = ImageDraw.Draw(overlay)
  poly_pts = [tuple(p) for p in outer_px] + [tuple(outer_px[0])]
  draw.line(poly_pts, fill=(0, 220, 255, 220), width=2)
  if hex_px:
    hex_pts = [tuple(p) for p in hex_px] + [tuple(hex_px[0])]
    draw.line(hex_pts, fill=(0, 220, 255, 220), width=2)
  draw.rectangle([mod_px_x, mod_px_y, mod_px_x + mod_px_w, mod_px_y + mod_px_h], outline=(255, 215, 0, 220), width=2)
  draw.rectangle([act_px_x, act_px_y, act_px_x + act_px_w, act_px_y + act_px_h], outline=(0, 255, 100, 255), width=3)
  draw.text((act_px_x + 8, act_px_y + 8), "U2 Screen Active (240x320)", fill=(0, 255, 100, 255))
  sw2_r = sw2["radius_px"]
  draw.ellipse([sw2["x_px"] - sw2_r, sw2["y_px"] - sw2_r, sw2["x_px"] + sw2_r, sw2["y_px"] + sw2_r], outline=(255, 120, 0, 255), width=2)
  draw.text((sw2["x_px"] - 24, sw2["y_px"] - sw2_r - 16), "SW2 (5-way)", fill=(255, 120, 0, 255))
  led_pts = [(l["x_px"], l["y_px"]) for l in geom["led_chain"]]
  draw.line(led_pts, fill=(255, 60, 60, 180), width=2)
  for led in geom["led_chain"]:
    lx, ly = led["x_px"], led["y_px"]
    draw.ellipse([lx - 9, ly - 9, lx + 9, ly + 9], fill=(255, 30, 60, 190), outline=(255, 255, 255, 255), width=2)
    label = f"{led['index']}:{led['ref']}"
    tx = lx + 12 if led["index"] < 10 else lx - 48
    draw.rectangle([tx - 2, ly - 7, tx + 38, ly + 7], fill=(0, 0, 0, 190))
    draw.text((tx, ly - 6), label, fill=(255, 255, 100, 255))
  overlay_path.parent.mkdir(parents=True, exist_ok=True)
  overlay.save(overlay_path)
  print(f"[2025] Wrote {layout_path}, {board_png_path}, {overlay_path}")


# ============================================================================
# 2026 ("Orion Trail" / Apollo) extraction & rendering
# ============================================================================

def extract_geometry_2026(tree):
  footprints, _edge_segments, top_polys, top_circles = parse_footprints_and_graphics(tree)

  edge_polys = [p["pts"] for p in top_polys if p["layer"] == "Edge.Cuts" and len(p["pts"]) >= 3]
  if not edge_polys:
    raise RuntimeError("Could not find Edge.Cuts gr_poly in Apollo.kicad_pcb")
  edge_polys.sort(key=len, reverse=True)
  outer_loop = edge_polys[0]
  if len(outer_loop) > 1 and outer_loop[0] == outer_loop[-1]:
    outer_loop = outer_loop[:-1]

  lanyard_circles = [c for c in top_circles if c["layer"] == "Edge.Cuts"]
  lanyard_hole = lanyard_circles[0] if lanyard_circles else None

  # Trace 9 WS2812C LEDs starting at U2 (RP2350A-QFN60) GPIO 24 ("NEO_PIX")
  gpio24_net, led_chain = trace_ws2812_chain(footprints, "U2", 24, 9)

  # Round 360x360 ST77916 screen porthole: gr_circle on F.SilkS at center (145.9, 72.67), r = 20.23 mm
  silk_circles = [c for c in top_circles if c["layer"] == "F.SilkS" and c["radius"] >= 15.0]
  if not silk_circles:
    raise RuntimeError("Could not find F.SilkS screen circle in Apollo.kicad_pcb")
  silk_circles.sort(key=lambda c: c["radius"], reverse=True)
  scr_circle = silk_circles[0]
  scr_cx, scr_cy = scr_circle["center"]
  mod_r = scr_circle["radius"]       # 20.23 mm (40.46 mm module diameter inside zodiac ring)
  act_r = round(mod_r - 1.15, 4)     # 19.08 mm (38.16 mm = 1.50" round 360x360 IPS active area)

  # SW4: 5-way navigation switch on left fin (Up, Down, Left, Right, Press=C)
  sw4 = footprints["SW4"]
  sw4_info = {
      "ref": "SW4",
      "kind": "5way",
      "label": "5-Way Joystick + C",
      "x_mm": round(sw4["x"], 4),
      "y_mm": round(sw4["y"], 4),
      "rot_deg": round(sw4["rot"], 2),
      "radius_mm": 7.5,
  }

  # SW2 (A button, GPIO17/BTN1) and SW3 (B button, GPIO16/BTN2) on right fin
  sw2 = footprints["SW2"]
  sw2_info = {
      "ref": "SW2",
      "kind": "push",
      "name": "A",
      "bit": 64,
      "gpio": 17,
      "x_mm": round(sw2["x"], 4),
      "y_mm": round(sw2["y"], 4),
      "rot_deg": round(sw2["rot"], 2),
      "radius_mm": 4.4,
  }

  sw3 = footprints["SW3"]
  sw3_info = {
      "ref": "SW3",
      "kind": "push",
      "name": "B",
      "bit": 32,
      "gpio": 16,
      "x_mm": round(sw3["x"], 4),
      "y_mm": round(sw3["y"], 4),
      "rot_deg": round(sw3["rot"], 2),
      "radius_mm": 4.4,
  }

  sw1 = footprints["SW1"]
  sw1_xs = [p[0] for l in sw1["lines"] if l["layer"] == "B.SilkS" for p in (l["start"], l["end"])]
  sw1_ys = [p[1] for l in sw1["lines"] if l["layer"] == "B.SilkS" for p in (l["start"], l["end"])]
  sw1_info = {
      "ref": "SW1",
      "x_mm": round(sw1["x"], 4),
      "y_mm": round(sw1["y"], 4),
      "rot_deg": round(sw1["rot"], 2),
      "w_mm": round(max(sw1_xs) - min(sw1_xs), 4) if sw1_xs else 3.6,
      "h_mm": round(max(sw1_ys) - min(sw1_ys), 4) if sw1_ys else 7.0,
  }

  # Minibadge slots (SAINTCON-Minibadge / MiniBadge_Slot_Lock)
  minibadges = []
  for ref, fp in footprints.items():
    if "MiniBadge" in fp["lib"] or "Minibadge" in (fp["val"] or ""):
      pad_xs = [
          kicad_local_to_board(fp["x"], fp["y"], fp["rot"], fp["layer"], p["lx"], p["ly"])[0]
          for p in fp["pads"]
      ]
      pad_ys = [
          kicad_local_to_board(fp["x"], fp["y"], fp["rot"], fp["layer"], p["lx"], p["ly"])[1]
          for p in fp["pads"]
      ]
      cx = (min(pad_xs) + max(pad_xs)) / 2.0
      cy = (min(pad_ys) + max(pad_ys)) / 2.0
      minibadges.append({
          "ref": ref,
          "x_mm": round(cx, 4),
          "y_mm": round(cy, 4),
          "w_mm": round(max(pad_xs) - min(pad_xs) + 2.54, 4),
          "h_mm": round(max(pad_ys) - min(pad_ys) + 2.54, 4),
          "origin_x_mm": round(fp["x"], 4),
          "origin_y_mm": round(fp["y"], 4),
          "rot_deg": round(fp["rot"], 2),
      })
  minibadges.sort(key=lambda m: (round(m["y_mm"], 1), round(m["x_mm"], 1)))
  for idx, mb in enumerate(minibadges):
    mb["slot"] = idx

  return {
      "outer_loop": outer_loop,
      "lanyard_hole": lanyard_hole,
      "led_chain": led_chain,
      "gpio24_net": gpio24_net,
      "screen_center_mm": (scr_cx, scr_cy),
      "screen_active_r_mm": act_r,
      "screen_module_r_mm": mod_r,
      "sw4": sw4_info,
      "sw2": sw2_info,
      "sw3": sw3_info,
      "sw1": sw1_info,
      "minibadges": minibadges,
  }


def ensure_kicad_renders_2026(pcb_src: Path, geom, out_board_png: Path):
  work_dir = BUILD_DIR / "kicad-2026"
  work_dir.mkdir(parents=True, exist_ok=True)
  k9_pcb = work_dir / "Apollo_k9.kicad_pcb"
  render_3d_png = work_dir / "render-3d.png"
  fmask_svg = work_dir / "layer-fmask.svg"
  fsilks_sub_svg = work_dir / "layer-fsilks-sub.svg"
  fcu_svg = work_dir / "layer-fcu.svg"

  if not (
      render_3d_png.exists()
      and fmask_svg.exists()
      and fsilks_sub_svg.exists()
      and fcu_svg.exists()
  ):
    raw_txt = pcb_src.read_text(encoding="utf-8", errors="replace")
    k9_txt = downgrade_kicad10_to_kicad9(raw_txt)
    k9_txt = k9_txt.replace("(subtractmaskfromsilk no)", "(subtractmaskfromsilk yes)")
    k9_txt = k9_txt.replace('(copper_finish "None")', '(copper_finish "ENIG")')
    k9_pcb.write_text(k9_txt, encoding="utf-8")

    uid_gid = f"{os.getuid()}:{os.getgid()}"
    rel_work = work_dir.relative_to(ROOT)
    cmd = (
        f"kicad-cli pcb render --side top --background transparent --quality basic"
        f" -w 1800 -h 3600 -o {rel_work}/render-3d.png {rel_work}/Apollo_k9.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.Mask --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o {rel_work}/layer-fmask.svg {rel_work}/Apollo_k9.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.SilkS --subtract-soldermask --black-and-white"
        f" --page-size-mode 0 --exclude-drawing-sheet -o {rel_work}/layer-fsilks-sub.svg {rel_work}/Apollo_k9.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.Cu --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o {rel_work}/layer-fcu.svg {rel_work}/Apollo_k9.kicad_pcb"
    )
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-u",
            uid_gid,
            "-v",
            f"{ROOT}:/w",
            "-w",
            "/w",
            "kicad/kicad:9.0",
            "sh",
            "-c",
            cmd,
        ],
        check=True,
    )

  im3d = Image.open(render_3d_png).convert("RGBA")
  W_full, H_full = im3d.size
  arr3d = np.array(im3d, dtype=np.float32)
  ys, xs = np.where(arr3d[:, :, 3] > 0)
  xmin_px_full, xmax_px_full = int(xs.min()), int(xs.max())
  ymin_px_full, ymax_px_full = int(ys.min()), int(ys.max())

  oxs = [p[0] for p in geom["outer_loop"]]
  oys = [p[1] for p in geom["outer_loop"]]
  xmin_mm, xmax_mm = min(oxs), max(oxs)
  ymin_mm, ymax_mm = min(oys), max(oys)

  scale_full = (
      (xmax_px_full - xmin_px_full) / (xmax_mm - xmin_mm)
      + (ymax_px_full - ymin_px_full) / (ymax_mm - ymin_mm)
  ) / 2.0
  x0_mm = xmin_mm - xmin_px_full / scale_full
  y0_mm = ymin_mm - ymin_px_full / scale_full
  w_mm = W_full / scale_full
  h_mm = H_full / scale_full

  align_svg_layers(
      work_dir,
      ("layer-fmask", "layer-fsilks-sub", "layer-fcu"),
      W_full,
      H_full,
      x0_mm,
      y0_mm,
      w_mm,
      h_mm,
  )

  board_alpha = arr3d[:, :, 3] / 255.0
  fmask = np.array(Image.open(work_dir / "layer-fmask-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fsilks = np.array(Image.open(work_dir / "layer-fsilks-sub-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fcu = np.array(Image.open(work_dir / "layer-fcu-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0

  lum3d = (0.299 * arr3d[:, :, 0] + 0.587 * arr3d[:, :, 1] + 0.114 * arr3d[:, :, 2]) / 255.0
  out = np.zeros((H_full, W_full, 4), dtype=np.float32)

  # Apollo stackup specifies White solder mask (F.Mask) + Black silkscreen (F.SilkS)
  # Warm satin white mask (#EFF1F4) with subtle copper trace relief beneath solder mask
  base_r, base_g, base_b = 239.0, 241.0, 244.0
  trace_relief = fcu * (1.0 - fmask) * (-7.0)
  out[:, :, 0] = base_r + trace_relief + (lum3d - 0.88) * 18.0
  out[:, :, 1] = base_g + trace_relief + (lum3d - 0.88) * 18.0
  out[:, :, 2] = base_b + trace_relief + (lum3d - 0.88) * 18.0

  # Exposed F.Mask openings: warm ENIG gold where copper is present (rocket bands, stars,
  # edge connectors, pads) and warm amber FR4 where bare dielectric is exposed
  yy, xx = np.mgrid[0:H_full, 0:W_full]
  tex = np.sin(xx * 0.06 + yy * 0.05) * 3.0 + np.cos(xx * 0.03 - yy * 0.08) * 3.0
  fr4_r = 196.0 + tex
  fr4_g = 158.0 + tex * 0.8
  fr4_b = 82.0 + tex * 0.4

  enig_r = 216.0 + tex * 0.8 + (lum3d - 0.7) * 20.0
  enig_g = 176.0 + tex * 0.6 + (lum3d - 0.7) * 16.0
  enig_b = 68.0 + tex * 0.3 + (lum3d - 0.7) * 10.0

  exposed_r = fr4_r * (1.0 - fcu) + enig_r * fcu
  exposed_g = fr4_g * (1.0 - fcu) + enig_g * fcu
  exposed_b = fr4_b * (1.0 - fcu) + enig_b * fcu

  out[:, :, 0] = out[:, :, 0] * (1.0 - fmask) + exposed_r * fmask
  out[:, :, 1] = out[:, :, 1] * (1.0 - fmask) + exposed_g * fmask
  out[:, :, 2] = out[:, :, 2] * (1.0 - fmask) + exposed_b * fmask

  # Crisp matte black silkscreen (zodiac constellations, SAINTCON 2026 lettering, Saturn, atom)
  silk_r, silk_g, silk_b = 24.0, 26.0, 30.0
  out[:, :, 0] = out[:, :, 0] * (1.0 - fsilks) + silk_r * fsilks
  out[:, :, 1] = out[:, :, 1] * (1.0 - fsilks) + silk_g * fsilks
  out[:, :, 2] = out[:, :, 2] * (1.0 - fsilks) + silk_b * fsilks

  out[:, :, 3] = board_alpha * 255.0
  res_full = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), mode="RGBA")

  W_out = W_full // 2
  H_out = H_full // 2
  scale_out = scale_full / 2.0
  res_out = res_full.resize((W_out, H_out), Image.Resampling.LANCZOS)

  draw = ImageDraw.Draw(res_out)

  def mm_to_px(xmm, ymm):
    return (xmm - x0_mm) * scale_out, (ymm - y0_mm) * scale_out

  # Physical round ST77916 LCD porthole bezel & dark active glass inside zodiac ring
  scr_cx, scr_cy = geom["screen_center_mm"]
  mod_r = geom["screen_module_r_mm"]
  act_r = geom["screen_active_r_mm"]
  mcx, mcy = mm_to_px(scr_cx, scr_cy)
  mr_px = mod_r * scale_out
  ar_px = act_r * scale_out
  draw.ellipse(
      [mcx - mr_px, mcy - mr_px, mcx + mr_px, mcy + mr_px],
      fill=(20, 23, 28, 255),
      outline=(58, 64, 74, 255),
      width=2,
  )
  draw.ellipse(
      [mcx - ar_px, mcy - ar_px, mcx + ar_px, mcy + ar_px],
      fill=(8, 10, 14, 255),
      outline=(36, 40, 48, 255),
      width=1,
  )

  # Physical SMD tactile switch housings & dark actuator caps for SW2 (A) and SW3 (B) on right fin
  for sw in (geom["sw2"], geom["sw3"]):
    bx, by = mm_to_px(sw["x_mm"], sw["y_mm"])
    cap_r = 2.0 * scale_out
    ring_r = 2.7 * scale_out
    draw.ellipse(
        [bx - ring_r, by - ring_r, bx + ring_r, by + ring_r],
        fill=(210, 214, 220, 235),
        outline=(95, 102, 112, 255),
        width=2,
    )
    draw.ellipse(
        [bx - cap_r, by - cap_r, bx + cap_r, by + cap_r],
        fill=(34, 38, 44, 255),
        outline=(70, 76, 86, 255),
        width=1,
    )

  # Physical SW4 5-way navigation switch housing on left fin
  jx, jy = mm_to_px(geom["sw4"]["x_mm"], geom["sw4"]["y_mm"])
  j_base_r = 4.2 * scale_out
  j_hat_r = 2.3 * scale_out
  draw.ellipse(
      [jx - j_base_r, jy - j_base_r, jx + j_base_r, jy + j_base_r],
      fill=(205, 210, 216, 220),
      outline=(95, 102, 112, 255),
      width=2,
  )
  draw.ellipse(
      [jx - j_hat_r, jy - j_hat_r, jx + j_hat_r, jy + j_hat_r],
      fill=(38, 42, 48, 255),
      outline=(80, 86, 96, 255),
      width=1,
  )

  out_board_png.parent.mkdir(parents=True, exist_ok=True)
  res_out.save(out_board_png, optimize=True)

  return {
      "width_px": W_out,
      "height_px": H_out,
      "scale_px_per_mm": round(scale_out, 6),
      "origin_x_mm": round(x0_mm, 6),
      "origin_y_mm": round(y0_mm, 6),
  }, res_out


def build_2026(pcb_path: Path):
  if not pcb_path.exists():
    raise FileNotFoundError(f"2026 KiCad PCB not found: {pcb_path}")

  out_dir = BADGES_DIR / "2026"
  layout_path = out_dir / "layout.json"
  board_png_path = out_dir / "board.png"
  overlay_path = BUILD_DIR / "check-overlay-2026.png"

  pcb_text = pcb_path.read_text(encoding="utf-8", errors="replace")
  tree = parse_sexp(pcb_text)
  geom = extract_geometry_2026(tree)
  img_meta, board_img = ensure_kicad_renders_2026(pcb_path, geom, board_png_path)

  scale = img_meta["scale_px_per_mm"]
  x0 = img_meta["origin_x_mm"]
  y0 = img_meta["origin_y_mm"]

  def to_px(xmm, ymm):
    return round((xmm - x0) * scale, 2), round((ymm - y0) * scale, 2)

  outer_mm = [[round(x, 4), round(y, 4)] for x, y in geom["outer_loop"]]
  outer_px = [list(to_px(x, y)) for x, y in geom["outer_loop"]]

  oxs = [p[0] for p in outer_mm]
  oys = [p[1] for p in outer_mm]
  pxs = [p[0] for p in outer_px]
  pys = [p[1] for p in outer_px]

  scr_cx, scr_cy = geom["screen_center_mm"]
  act_r = geom["screen_active_r_mm"]
  mod_r = geom["screen_module_r_mm"]

  act_x = scr_cx - act_r
  act_y = scr_cy - act_r
  act_d = act_r * 2.0
  mod_x = scr_cx - mod_r
  mod_y = scr_cy - mod_r
  mod_d = mod_r * 2.0

  cx_px, cy_px = to_px(scr_cx, scr_cy)
  act_r_px = round(act_r * scale, 2)
  mod_r_px = round(mod_r * scale, 2)
  act_px_x, act_px_y = to_px(act_x, act_y)
  act_px_d = round(act_d * scale, 2)
  mod_px_x, mod_px_y = to_px(mod_x, mod_y)
  mod_px_d = round(mod_d * scale, 2)

  screen_obj = {
      "ref": "J22",
      "part": "ST77916-360x360-ROUND",
      "shape": "round",
      "width_dots": 360,
      "height_dots": 360,
      "cx_mm": round(scr_cx, 4),
      "cy_mm": round(scr_cy, 4),
      "r_mm": round(act_r, 4),
      "cx_px": cx_px,
      "cy_px": cy_px,
      "r_px": act_r_px,
      "x_mm": round(act_x, 4),
      "y_mm": round(act_y, 4),
      "w_mm": round(act_d, 4),
      "h_mm": round(act_d, 4),
      "x_px": act_px_x,
      "y_px": act_px_y,
      "w_px": act_px_d,
      "h_px": act_px_d,
      "module_mm": {"x": round(mod_x, 4), "y": round(mod_y, 4), "w": round(mod_d, 4), "h": round(mod_d, 4), "r": round(mod_r, 4)},
      "module_px": {"x": mod_px_x, "y": mod_px_y, "w": mod_px_d, "h": mod_px_d, "r": mod_r_px},
  }

  for sw in (geom["sw4"], geom["sw2"], geom["sw3"]):
    sw["x_px"], sw["y_px"] = to_px(sw["x_mm"], sw["y_mm"])
    sw["radius_px"] = round(sw["radius_mm"] * scale, 2)

  sw1 = geom["sw1"]
  sw1["x_px"], sw1["y_px"] = to_px(sw1["x_mm"], sw1["y_mm"])
  sw1["w_px"] = round(sw1["w_mm"] * scale, 2)
  sw1["h_px"] = round(sw1["h_mm"] * scale, 2)

  for mb in geom["minibadges"]:
    mb["x_px"], mb["y_px"] = to_px(mb["x_mm"], mb["y_mm"])
    mb["w_px"] = round(mb["w_mm"] * scale, 2)
    mb["h_px"] = round(mb["h_mm"] * scale, 2)

  for led in geom["led_chain"]:
    led["x_px"], led["y_px"] = to_px(led["x_mm"], led["y_mm"])

  lanyard_obj = None
  if geom["lanyard_hole"]:
    lx_mm, ly_mm = geom["lanyard_hole"]["center"]
    lr_mm = geom["lanyard_hole"]["radius"]
    lx_px, ly_px = to_px(lx_mm, ly_mm)
    lanyard_obj = {
        "cx_mm": round(lx_mm, 4),
        "cy_mm": round(ly_mm, 4),
        "r_mm": round(lr_mm, 4),
        "cx_px": lx_px,
        "cy_px": ly_px,
        "r_px": round(lr_mm * scale, 2),
    }

  layout = {
      "board_image": {"file": "board.png", **img_meta},
      "outline": {
          "bbox_mm": {"x": round(min(oxs), 4), "y": round(min(oys), 4), "w": round(max(oxs) - min(oxs), 4), "h": round(max(oys) - min(oys), 4)},
          "bbox_px": {"x": round(min(pxs), 2), "y": round(min(pys), 2), "w": round(max(pxs) - min(pxs), 2), "h": round(max(pys) - min(pys), 2)},
          "polygon_mm": outer_mm,
          "polygon_px": outer_px,
          "lanyard_hole": lanyard_obj,
      },
      "screen": screen_obj,
      # Primary 5-way nav switch (SW4 on 2026; also aliased under nav_switch)
      "nav_switch": geom["sw4"],
      "sw4": geom["sw4"],
      "action_buttons": [geom["sw2"], geom["sw3"]],
      "sw2": geom["sw2"],
      "sw3": geom["sw3"],
      "sw1": sw1,
      "minibadges": geom["minibadges"],
      "led_chain_source": {
          "mcu_ref": "U2",
          "gpio": 24,
          "start_net": geom["gpio24_net"],
          "order_refs": [l["ref"] for l in geom["led_chain"]],
      },
      "leds": geom["led_chain"],
  }

  out_dir.mkdir(parents=True, exist_ok=True)
  layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

  # Verification overlay image (emulator/build/check-overlay-2026.png)
  overlay = board_img.copy()
  draw = ImageDraw.Draw(overlay)

  poly_pts = [tuple(p) for p in outer_px] + [tuple(outer_px[0])]
  draw.line(poly_pts, fill=(0, 180, 255, 230), width=2)

  draw.ellipse(
      [cx_px - mod_r_px, cy_px - mod_r_px, cx_px + mod_r_px, cy_px + mod_r_px],
      outline=(255, 200, 0, 230),
      width=2,
  )
  draw.ellipse(
      [cx_px - act_r_px, cy_px - act_r_px, cx_px + act_r_px, cy_px + act_r_px],
      outline=(0, 240, 100, 255),
      width=3,
  )
  draw.text((cx_px - 75, cy_px - 6), "ST77916 Round (360x360)", fill=(0, 255, 110, 255))

  for sw, label, col in (
      (geom["sw4"], "SW4 (5-way + C)", (255, 110, 0, 255)),
      (geom["sw2"], "SW2 (Btn A)", (0, 190, 80, 255)),
      (geom["sw3"], "SW3 (Btn B)", (30, 110, 255, 255)),
  ):
    r = sw["radius_px"]
    draw.ellipse([sw["x_px"] - r, sw["y_px"] - r, sw["x_px"] + r, sw["y_px"] + r], outline=col, width=3)
    draw.rectangle([sw["x_px"] - 36, sw["y_px"] - r - 18, sw["x_px"] + 58, sw["y_px"] - r - 3], fill=(0, 0, 0, 190))
    draw.text((sw["x_px"] - 33, sw["y_px"] - r - 17), label, fill=col)

  for mb in geom["minibadges"]:
    draw.rectangle(
        [mb["x_px"] - mb["w_px"] / 2, mb["y_px"] - mb["h_px"] / 2, mb["x_px"] + mb["w_px"] / 2, mb["y_px"] + mb["h_px"] / 2],
        outline=(150, 90, 240, 220),
        width=2,
    )

  led_pts = [(l["x_px"], l["y_px"]) for l in geom["led_chain"]]
  draw.line(led_pts, fill=(255, 40, 60, 190), width=2)
  for led in geom["led_chain"]:
    lx, ly = led["x_px"], led["y_px"]
    draw.ellipse([lx - 10, ly - 10, lx + 10, ly + 10], fill=(255, 30, 60, 210), outline=(255, 255, 255, 255), width=2)
    label = f"{led['index']}:{led['ref']}"
    tx = lx + 13 if lx < cx_px else lx - 52
    draw.rectangle([tx - 2, ly - 7, tx + 42, ly + 7], fill=(0, 0, 0, 200))
    draw.text((tx, ly - 6), label, fill=(255, 255, 100, 255))

  overlay_path.parent.mkdir(parents=True, exist_ok=True)
  overlay.save(overlay_path)
  print(f"[2026] Wrote {layout_path}")
  print(f"[2026] Wrote {board_png_path} ({img_meta['width_px']}x{img_meta['height_px']})")
  print(f"[2026] Wrote {overlay_path}")
  print(
      "[2026] LED chain order:",
      " -> ".join(f"{l['index']}:{l['ref']}({l['din_net']})" for l in geom["led_chain"]),
  )


# ============================================================================
# 2024 ("Wrist Communicator" / Tower Battle) extraction & rendering
# ============================================================================

def _sample_3pt_arc(p1, pm, p2, steps=12):
  """Sample a KiCad 3-point gr_arc (start, mid, end) into polyline points."""
  x1, y1 = p1
  x2, y2 = pm
  x3, y3 = p2
  d = 2.0 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
  if abs(d) < 1e-9:
    return [p1, p2]
  ux = ((x1 * x1 + y1 * y1) * (y2 - y3) + (x2 * x2 + y2 * y2) * (y3 - y1) + (x3 * x3 + y3 * y3) * (y1 - y2)) / d
  uy = ((x1 * x1 + y1 * y1) * (x3 - x2) + (x2 * x2 + y2 * y2) * (x1 - x3) + (x3 * x3 + y3 * y3) * (x2 - x1)) / d
  r = math.hypot(x1 - ux, y1 - uy)
  a1 = math.atan2(y1 - uy, x1 - ux)
  am = math.atan2(y2 - uy, x2 - ux)
  a3 = math.atan2(y3 - uy, x3 - ux)

  def norm_pos(a):
    while a < 0:
      a += 2.0 * math.pi
    while a >= 2.0 * math.pi:
      a -= 2.0 * math.pi
    return a

  d_ccw_m = norm_pos(am - a1)
  d_ccw_3 = norm_pos(a3 - a1)
  sweep = d_ccw_3 if d_ccw_m <= d_ccw_3 else d_ccw_3 - 2.0 * math.pi
  pts = []
  for i in range(steps + 1):
    ang = a1 + sweep * (i / steps)
    pts.append((ux + r * math.cos(ang), uy + r * math.sin(ang)))
  return pts


def extract_geometry_2024(main_tree, top_tree):
  footprints, _edge_segments, _top_polys, _top_circles = parse_footprints_and_graphics(main_tree)

  # Parse Edge.Cuts gr_line + gr_arc segments from Saintcon2024.kicad_pcb (84x55 mm rounded rect)
  segs = []
  for node in main_tree:
    if not isinstance(node, list) or not node:
      continue
    if node[0] in ("gr_line", "gr_arc"):
      layer = None
      s_pt = m_pt = e_pt = None
      for sub in node[1:]:
        if isinstance(sub, list) and sub:
          if sub[0] == "layer":
            layer = sub[1]
          elif sub[0] == "start":
            s_pt = (float(sub[1]), float(sub[2]))
          elif sub[0] == "mid":
            m_pt = (float(sub[1]), float(sub[2]))
          elif sub[0] == "end":
            e_pt = (float(sub[1]), float(sub[2]))
      if layer == "Edge.Cuts" and s_pt and e_pt:
        if node[0] == "gr_arc" and m_pt:
          arc_pts = _sample_3pt_arc(s_pt, m_pt, e_pt, steps=10)
          for i in range(len(arc_pts) - 1):
            segs.append((arc_pts[i], arc_pts[i + 1]))
        else:
          segs.append((s_pt, e_pt))

  if not segs:
    raise RuntimeError("Could not find Edge.Cuts outline in Saintcon2024.kicad_pcb")

  # Chain Edge.Cuts segments into ordered outer_loop
  unused = list(segs)
  outer_loop = [unused[0][0], unused[0][1]]
  unused.pop(0)
  while unused:
    cur = outer_loop[-1]
    best_i = None
    best_rev = False
    best_d = 1e9
    for idx, (a, b) in enumerate(unused):
      da = math.hypot(a[0] - cur[0], a[1] - cur[1])
      db = math.hypot(b[0] - cur[0], b[1] - cur[1])
      if da < best_d:
        best_d = da
        best_i = idx
        best_rev = False
      if db < best_d:
        best_d = db
        best_i = idx
        best_rev = True
    a, b = unused.pop(best_i)
    nxt = a if best_rev else b
    if math.hypot(nxt[0] - outer_loop[0][0], nxt[1] - outer_loop[0][1]) > 1e-3:
      outer_loop.append(nxt)

  # Parse TopBoard screen cutout gr_rect on Edge.Cuts (59.0 x 44.8 mm)
  cutout_rect = None
  for node in top_tree:
    if isinstance(node, list) and node and node[0] == "gr_rect":
      layer = None
      s_pt = e_pt = None
      for sub in node[1:]:
        if isinstance(sub, list) and sub:
          if sub[0] == "layer":
            layer = sub[1]
          elif sub[0] == "start":
            s_pt = (float(sub[1]), float(sub[2]))
          elif sub[0] == "end":
            e_pt = (float(sub[1]), float(sub[2]))
      if layer == "Edge.Cuts" and s_pt and e_pt:
        cutout_rect = (
            min(s_pt[0], e_pt[0]),
            min(s_pt[1], e_pt[1]),
            abs(e_pt[0] - s_pt[0]),
            abs(e_pt[1] - s_pt[1]),
        )
        break
  if not cutout_rect:
    raise RuntimeError("Could not find Edge.Cuts screen cutout gr_rect in Saintcon2024Top.kicad_pcb")

  # Extract LCD_2_8 module & active area from MainBoard footprint
  lcd_fp = None
  for fp in footprints.values():
    if "LCD_2_8" in fp["lib"]:
      lcd_fp = fp
      break
  if not lcd_fp:
    raise RuntimeError("Could not find LCD_Displays:LCD_2_8 footprint in Saintcon2024.kicad_pcb")

  dwg_rects = [r for r in lcd_fp["rects"] if r["layer"] == "Dwgs.User"]
  if len(dwg_rects) < 2:
    raise RuntimeError("Expected module + active Dwgs.User rects in LCD_2_8 footprint")
  dwg_rects.sort(key=lambda r: (r["x2"] - r["x1"]) * (r["y2"] - r["y1"]), reverse=True)
  mod_r = dwg_rects[0]
  va_r = dwg_rects[1]
  mod_mm = (mod_r["x1"], mod_r["y1"], mod_r["x2"] - mod_r["x1"], mod_r["y2"] - mod_r["y1"])
  # 2.8" 320x240 ST7789 active area is 57.60 x 43.20 mm (0.18 mm dot pitch, exact 4:3),
  # centered horizontally on the LCD viewing area (cx=139.75) and framed by TopBoard's 59.0x44.8 cutout (cy=127.50)
  va_cx = (va_r["x1"] + va_r["x2"]) / 2.0
  cut_cy = cutout_rect[1] + cutout_rect[3] / 2.0
  act_w, act_h = 57.60, 43.20
  act_mm = (va_cx - act_w / 2.0, cut_cy - act_h / 2.0, act_w, act_h)

  # Mounting holes (4x 4-40 corner screws connecting MainBoard and TopBoard)
  screws = []
  for node in top_tree:
    if isinstance(node, list) and node and node[0] == "footprint" and "MountingHole" in node[1]:
      for sub in node[2:]:
        if isinstance(sub, list) and sub and sub[0] == "at":
          screws.append((float(sub[1]), float(sub[2])))
          break

  sw1 = footprints["SW1"]
  sw1_info = {
      "ref": "SW1",
      "x_mm": round(sw1["x"], 4),
      "y_mm": round(sw1["y"], 4),
      "rot_deg": round(sw1["rot"], 2),
      "w_mm": 8.6,
      "h_mm": 3.6,
  }

  usb_j1 = footprints["J1"]
  usb_info = {
      "ref": "J1",
      "x_mm": round(usb_j1["x"], 4),
      "y_mm": round(usb_j1["y"], 4),
      "rot_deg": round(usb_j1["rot"], 2),
  }

  ir_tx = footprints["D1"]
  ir_rx = footprints["U2"]

  return {
      "outer_loop": outer_loop,
      "cutout_mm": cutout_rect,
      "screen_module_mm": mod_mm,
      "screen_active_mm": act_mm,
      "screws_mm": screws,
      "sw1": sw1_info,
      "usb_j1": usb_info,
      "ir_tx": {"ref": "D1", "x_mm": round(ir_tx["x"], 4), "y_mm": round(ir_tx["y"], 4)},
      "ir_rx": {"ref": "U2", "x_mm": round(ir_rx["x"], 4), "y_mm": round(ir_rx["y"], 4)},
  }


def ensure_kicad_renders_2024(main_pcb: Path, top_pcb: Path, geom, out_board_png: Path):
  work_dir = BUILD_DIR / "kicad-2024"
  work_dir.mkdir(parents=True, exist_ok=True)
  top_mod_pcb = work_dir / "Saintcon2024Top_render.kicad_pcb"
  render_3d_png = work_dir / "top-render-3d.png"
  fmask_svg = work_dir / "top-layer-fmask.svg"
  fsilks_sub_svg = work_dir / "top-layer-fsilks-sub.svg"
  fcu_svg = work_dir / "top-layer-fcu.svg"

  if not (
      render_3d_png.exists()
      and fmask_svg.exists()
      and fsilks_sub_svg.exists()
      and fcu_svg.exists()
  ):
    raw_txt = top_pcb.read_text(encoding="utf-8", errors="replace")
    mod_txt = raw_txt.replace("(subtractmaskfromsilk no)", "(subtractmaskfromsilk yes)")
    top_mod_pcb.write_text(mod_txt, encoding="utf-8")

    uid_gid = f"{os.getuid()}:{os.getgid()}"
    rel_work = work_dir.relative_to(ROOT)
    cmd = (
        f"kicad-cli pcb render --side top --background transparent --quality basic"
        f" -w 2400 -h 1650 -o {rel_work}/top-render-3d.png {rel_work}/Saintcon2024Top_render.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.Mask --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o {rel_work}/top-layer-fmask.svg {rel_work}/Saintcon2024Top_render.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.SilkS --subtract-soldermask --black-and-white"
        f" --page-size-mode 0 --exclude-drawing-sheet -o {rel_work}/top-layer-fsilks-sub.svg {rel_work}/Saintcon2024Top_render.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.Cu --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o {rel_work}/top-layer-fcu.svg {rel_work}/Saintcon2024Top_render.kicad_pcb"
    )
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-u",
            uid_gid,
            "-v",
            f"{ROOT}:/w",
            "-w",
            "/w",
            "kicad/kicad:9.0",
            "sh",
            "-c",
            cmd,
        ],
        check=True,
    )

  im3d = Image.open(render_3d_png).convert("RGBA")
  W_full, H_full = im3d.size
  arr3d = np.array(im3d, dtype=np.float32)
  ys, xs = np.where(arr3d[:, :, 3] > 0)
  xmin_px_full, xmax_px_full = int(xs.min()), int(xs.max())
  ymin_px_full, ymax_px_full = int(ys.min()), int(ys.max())

  oxs = [p[0] for p in geom["outer_loop"]]
  oys = [p[1] for p in geom["outer_loop"]]
  xmin_mm, xmax_mm = min(oxs), max(oxs)
  ymin_mm, ymax_mm = min(oys), max(oys)

  scale_full = (
      (xmax_px_full - xmin_px_full) / (xmax_mm - xmin_mm)
      + (ymax_px_full - ymin_px_full) / (ymax_mm - ymin_mm)
  ) / 2.0
  x0_mm = xmin_mm - xmin_px_full / scale_full
  y0_mm = ymin_mm - ymin_px_full / scale_full
  w_mm = W_full / scale_full
  h_mm = H_full / scale_full

  align_svg_layers(
      work_dir,
      ("top-layer-fmask", "top-layer-fsilks-sub", "top-layer-fcu"),
      W_full,
      H_full,
      x0_mm,
      y0_mm,
      w_mm,
      h_mm,
  )

  board_alpha = arr3d[:, :, 3] / 255.0
  fmask = np.array(Image.open(work_dir / "top-layer-fmask-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fsilks = np.array(Image.open(work_dir / "top-layer-fsilks-sub-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fcu = np.array(Image.open(work_dir / "top-layer-fcu-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0

  lum3d = (0.299 * arr3d[:, :, 0] + 0.587 * arr3d[:, :, 1] + 0.114 * arr3d[:, :, 2]) / 255.0
  top_rgba = np.zeros((H_full, W_full, 4), dtype=np.float32)

  # Saintcon2024Top stackup specifies Green solder mask + Black silkscreen.
  # Isometric cube art uses 3 surfaces: green masked PCB, exposed FR4 dielectric, and exposed HASL/ENIG copper.
  mask_over_cu = fcu * (1.0 - fmask)
  base_r = 26.0 + mask_over_cu * 10.0 + (lum3d - 0.35) * 16.0
  base_g = 70.0 + mask_over_cu * 20.0 + (lum3d - 0.35) * 18.0
  base_b = 47.0 + mask_over_cu * 11.0 + (lum3d - 0.35) * 14.0

  yy, xx = np.mgrid[0:H_full, 0:W_full]
  tex = np.sin(xx * 0.06 + yy * 0.05) * 2.5 + np.cos(xx * 0.03 - yy * 0.08) * 2.5
  fr4_r = 128.0 + tex
  fr4_g = 120.0 + tex * 0.8
  fr4_b = 76.0 + tex * 0.5

  metal_r = 204.0 + tex * 0.6 + (lum3d - 0.5) * 24.0
  metal_g = 208.0 + tex * 0.6 + (lum3d - 0.5) * 24.0
  metal_b = 212.0 + tex * 0.6 + (lum3d - 0.5) * 24.0

  exposed_r = fr4_r * (1.0 - fcu) + metal_r * fcu
  exposed_g = fr4_g * (1.0 - fcu) + metal_g * fcu
  exposed_b = fr4_b * (1.0 - fcu) + metal_b * fcu

  top_rgba[:, :, 0] = base_r * (1.0 - fmask) + exposed_r * fmask
  top_rgba[:, :, 1] = base_g * (1.0 - fmask) + exposed_g * fmask
  top_rgba[:, :, 2] = base_b * (1.0 - fmask) + exposed_b * fmask

  silk_r, silk_g, silk_b = 34.0, 38.0, 42.0
  top_rgba[:, :, 0] = top_rgba[:, :, 0] * (1.0 - fsilks) + silk_r * fsilks
  top_rgba[:, :, 1] = top_rgba[:, :, 1] * (1.0 - fsilks) + silk_g * fsilks
  top_rgba[:, :, 2] = top_rgba[:, :, 2] * (1.0 - fsilks) + silk_b * fsilks
  top_rgba[:, :, 3] = board_alpha * 255.0

  top_full = Image.fromarray(np.clip(top_rgba, 0, 255).astype(np.uint8), mode="RGBA")

  # Tightly crop around the 84x55 mm assembly with a 2.5 mm margin for protruding USB-C & SW1
  margin_mm = 2.6
  crop_x0 = max(0, int(round((xmin_mm - margin_mm - x0_mm) * scale_full)))
  crop_y0 = max(0, int(round((ymin_mm - margin_mm - y0_mm) * scale_full)))
  crop_x1 = min(W_full, int(round((xmax_mm + margin_mm - x0_mm) * scale_full)))
  crop_y1 = min(H_full, int(round((ymax_mm + margin_mm - y0_mm) * scale_full)))
  if (crop_x1 - crop_x0) % 2 == 1:
    crop_x1 -= 1
  if (crop_y1 - crop_y0) % 2 == 1:
    crop_y1 -= 1

  top_cropped = top_full.crop((crop_x0, crop_y0, crop_x1, crop_y1))
  x0_crop_mm = x0_mm + crop_x0 / scale_full
  y0_crop_mm = y0_mm + crop_y0 / scale_full

  W_out = top_cropped.width // 2
  H_out = top_cropped.height // 2
  scale_out = scale_full / 2.0
  top_small = top_cropped.resize((W_out, H_out), Image.Resampling.LANCZOS)

  # Composite MainBoard protruding edge parts + LCD_2_8 glass underneath TopBoard faceplate
  canvas = Image.new("RGBA", (W_out, H_out), (0, 0, 0, 0))
  under = ImageDraw.Draw(canvas)

  def mm_to_px(xmm, ymm):
    return (xmm - x0_crop_mm) * scale_out, (ymm - y0_crop_mm) * scale_out

  # Protruding USB-C receptacle tongue on left edge (J1 at y=113.0 mm, ~8.94 mm wide shell)
  ux0, uy0 = mm_to_px(xmin_mm - 1.35, geom["usb_j1"]["y_mm"] - 4.47)
  ux1, uy1 = mm_to_px(xmin_mm + 2.0, geom["usb_j1"]["y_mm"] + 4.47)
  under.rounded_rectangle([ux0, uy0, ux1, uy1], radius=3, fill=(168, 174, 182, 255), outline=(88, 94, 102, 255), width=2)

  # Protruding SW1 SPDT power slide switch actuator on bottom edge (SW1 at x=155.5 mm)
  sx0, sy0 = mm_to_px(geom["sw1"]["x_mm"] - 1.5, ymax_mm - 1.0)
  sx1, sy1 = mm_to_px(geom["sw1"]["x_mm"] + 1.5, ymax_mm + 1.45)
  under.rounded_rectangle([sx0, sy0, sx1, sy1], radius=2, fill=(36, 40, 46, 255), outline=(82, 88, 96, 255), width=1)

  # Subtle IR emitter (D1) & receiver (U2) domes peeking on right edge ("Don't Block")
  for ir_part, col in ((geom["ir_rx"], (32, 28, 38, 240)), (geom["ir_tx"], (56, 72, 96, 235))):
    ix0, iy0 = mm_to_px(xmax_mm - 1.0, ir_part["y_mm"] - 2.1)
    ix1, iy1 = mm_to_px(xmax_mm + 0.65, ir_part["y_mm"] + 2.1)
    under.ellipse([ix0, iy0, ix1, iy1], fill=col, outline=(24, 26, 30, 255), width=1)

  # MainBoard LCD_2_8 capacitive touch glass bezel visible inside the 59.0 x 44.8 mm TopBoard cutout
  cx_mm, cy_mm, cw_mm, ch_mm = geom["cutout_mm"]
  cx0, cy0 = mm_to_px(cx_mm - 0.4, cy_mm - 0.4)
  cx1, cy1 = mm_to_px(cx_mm + cw_mm + 0.4, cy_mm + ch_mm + 0.4)
  under.rectangle([cx0, cy0, cx1, cy1], fill=(18, 21, 26, 255))

  ax_mm, ay_mm, aw_mm, ah_mm = geom["screen_active_mm"]
  ax0, ay0 = mm_to_px(ax_mm, ay_mm)
  ax1, ay1 = mm_to_px(ax_mm + aw_mm, ay_mm + ah_mm)
  under.rectangle([ax0, ay0, ax1, ay1], fill=(7, 9, 13, 255), outline=(34, 39, 46, 255), width=1)

  # Alpha-composite TopBoard faceplate over MainBoard + LCD
  res_out = Image.alpha_composite(canvas, top_small)
  over = ImageDraw.Draw(res_out)

  # Inner cutout bevel shadow around the 59.0 x 44.8 mm faceplate opening
  w_cx0, w_cy0 = mm_to_px(cx_mm, cy_mm)
  w_cx1, w_cy1 = mm_to_px(cx_mm + cw_mm, cy_mm + ch_mm)
  over.rectangle([w_cx0, w_cy0, w_cx1, w_cy1], outline=(12, 16, 14, 235), width=2)

  # Four 4-40 pan-head corner screws fastening TopBoard to MainBoard standoffs
  for sx_mm, sy_mm in geom["screws_mm"]:
    px, py = mm_to_px(sx_mm, sy_mm)
    head_r = 2.65 * scale_out
    slot_r = 1.35 * scale_out
    over.ellipse(
        [px - head_r, py - head_r, px + head_r, py + head_r],
        fill=(182, 188, 194, 255),
        outline=(92, 98, 106, 255),
        width=2,
    )
    over.line([px - slot_r, py, px + slot_r, py], fill=(84, 90, 98, 255), width=2)
    over.line([px, py - slot_r, px, py + slot_r], fill=(84, 90, 98, 255), width=2)

  out_board_png.parent.mkdir(parents=True, exist_ok=True)
  res_out.save(out_board_png, optimize=True)

  return {
      "width_px": W_out,
      "height_px": H_out,
      "scale_px_per_mm": round(scale_out, 6),
      "origin_x_mm": round(x0_crop_mm, 6),
      "origin_y_mm": round(y0_crop_mm, 6),
  }, res_out


def build_2024(main_pcb_override: Path = None):
  work_dir = BUILD_DIR / "kicad-2024"
  work_dir.mkdir(parents=True, exist_ok=True)
  main_pcb = main_pcb_override if main_pcb_override else (work_dir / "Saintcon2024.kicad_pcb")
  top_pcb = work_dir / "Saintcon2024Top.kicad_pcb"

  if not main_pcb.exists() or not top_pcb.exists():
    main_zip, top_zip = find_2024_kicad_zips_default()
    if not main_zip.exists() or not top_zip.exists():
      raise FileNotFoundError(f"2024 KiCad project zips not found: {main_zip}, {top_zip}")
    if not main_pcb.exists():
      with zipfile.ZipFile(main_zip, "r") as zf:
        zf.extract("Saintcon2024.kicad_pcb", work_dir)
    if not top_pcb.exists():
      with zipfile.ZipFile(top_zip, "r") as zf:
        zf.extract("Saintcon2024Top.kicad_pcb", work_dir)

  out_dir = BADGES_DIR / "2024"
  layout_path = out_dir / "layout.json"
  board_png_path = out_dir / "board.png"
  overlay_path = BUILD_DIR / "check-overlay-2024.png"

  main_tree = parse_sexp(main_pcb.read_text(encoding="utf-8", errors="replace"))
  top_tree = parse_sexp(top_pcb.read_text(encoding="utf-8", errors="replace"))
  geom = extract_geometry_2024(main_tree, top_tree)
  img_meta, board_img = ensure_kicad_renders_2024(main_pcb, top_pcb, geom, board_png_path)

  scale = img_meta["scale_px_per_mm"]
  x0 = img_meta["origin_x_mm"]
  y0 = img_meta["origin_y_mm"]

  def to_px(xmm, ymm):
    return round((xmm - x0) * scale, 2), round((ymm - y0) * scale, 2)

  outer_mm = [[round(x, 4), round(y, 4)] for x, y in geom["outer_loop"]]
  outer_px = [list(to_px(x, y)) for x, y in geom["outer_loop"]]
  oxs = [p[0] for p in outer_mm]
  oys = [p[1] for p in outer_mm]
  pxs = [p[0] for p in outer_px]
  pys = [p[1] for p in outer_px]

  act_x, act_y, act_w, act_h = geom["screen_active_mm"]
  mod_x, mod_y, mod_w, mod_h = geom["screen_module_mm"]
  cut_x, cut_y, cut_w, cut_h = geom["cutout_mm"]

  act_px_x, act_px_y = to_px(act_x, act_y)
  act_px_w = round(act_w * scale, 2)
  act_px_h = round(act_h * scale, 2)
  mod_px_x, mod_px_y = to_px(mod_x, mod_y)
  mod_px_w = round(mod_w * scale, 2)
  mod_px_h = round(mod_h * scale, 2)
  cut_px_x, cut_px_y = to_px(cut_x, cut_y)
  cut_px_w = round(cut_w * scale, 2)
  cut_px_h = round(cut_h * scale, 2)

  screen_obj = {
      "ref": "J5",
      "part": "LCD-2.8-ST7789-GT911",
      "shape": "rect",
      "touch": True,
      "width_dots": 320,
      "height_dots": 240,
      "x_mm": round(act_x, 4),
      "y_mm": round(act_y, 4),
      "w_mm": round(act_w, 4),
      "h_mm": round(act_h, 4),
      "x_px": act_px_x,
      "y_px": act_px_y,
      "w_px": act_px_w,
      "h_px": act_px_h,
      "cutout_mm": {"x": round(cut_x, 4), "y": round(cut_y, 4), "w": round(cut_w, 4), "h": round(cut_h, 4)},
      "cutout_px": {"x": cut_px_x, "y": cut_px_y, "w": cut_px_w, "h": cut_px_h},
      "module_mm": {"x": round(mod_x, 4), "y": round(mod_y, 4), "w": round(mod_w, 4), "h": round(mod_h, 4)},
      "module_px": {"x": mod_px_x, "y": mod_px_y, "w": mod_px_w, "h": mod_px_h},
  }

  sw1 = dict(geom["sw1"])
  sw1["x_px"], sw1["y_px"] = to_px(sw1["x_mm"], sw1["y_mm"])
  sw1["w_px"] = round(sw1["w_mm"] * scale, 2)
  sw1["h_px"] = round(sw1["h_mm"] * scale, 2)

  ir_tx = dict(geom["ir_tx"])
  ir_tx["x_px"], ir_tx["y_px"] = to_px(ir_tx["x_mm"], ir_tx["y_mm"])
  ir_rx = dict(geom["ir_rx"])
  ir_rx["x_px"], ir_rx["y_px"] = to_px(ir_rx["x_mm"], ir_rx["y_mm"])

  layout = {
      "board_image": {"file": "board.png", **img_meta},
      "outline": {
          "bbox_mm": {"x": round(min(oxs), 4), "y": round(min(oys), 4), "w": round(max(oxs) - min(oxs), 4), "h": round(max(oys) - min(oys), 4)},
          "bbox_px": {"x": round(min(pxs), 2), "y": round(min(pys), 2), "w": round(max(pxs) - min(pxs), 2), "h": round(max(pys) - min(pys), 2)},
          "polygon_mm": outer_mm,
          "polygon_px": outer_px,
      },
      "screen": screen_obj,
      "sw1": sw1,
      "ir": {"tx": ir_tx, "rx": ir_rx},
      "minibadges": [],
      "leds": [],
  }

  out_dir.mkdir(parents=True, exist_ok=True)
  layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

  overlay = board_img.copy()
  draw = ImageDraw.Draw(overlay)
  poly_pts = [tuple(p) for p in outer_px] + [tuple(outer_px[0])]
  draw.line(poly_pts, fill=(0, 220, 255, 220), width=2)
  draw.rectangle([mod_px_x, mod_px_y, mod_px_x + mod_px_w, mod_px_y + mod_px_h], outline=(255, 140, 0, 180), width=2)
  draw.rectangle([cut_px_x, cut_px_y, cut_px_x + cut_px_w, cut_px_y + cut_px_h], outline=(255, 215, 0, 230), width=2)
  draw.rectangle([act_px_x, act_px_y, act_px_x + act_px_w, act_px_y + act_px_h], outline=(0, 255, 100, 255), width=3)
  draw.text((act_px_x + 10, act_px_y + 10), "J5 ST7789 + GT911 Touch (320x240)", fill=(0, 255, 100, 255))
  draw.text((ir_rx["x_px"] - 56, ir_rx["y_px"] - 6), "U2 IR RX", fill=(255, 180, 60, 255))
  draw.text((ir_tx["x_px"] - 56, ir_tx["y_px"] - 6), "D1 IR TX", fill=(255, 180, 60, 255))
  overlay_path.parent.mkdir(parents=True, exist_ok=True)
  overlay.save(overlay_path)
  print(f"[2024] Wrote {layout_path}, {board_png_path} ({img_meta['width_px']}x{img_meta['height_px']}), {overlay_path}")


# ============================================================================
# 2023 ("Incidents" Dungeon RPG) extraction & rendering
# ============================================================================

def extract_geometry_2023(tree):
  footprints, _edge_segments, _top_polys, _top_circles = parse_footprints_and_graphics(tree)

  # Parse Edge.Cuts fp_line + fp_arc segments inside the BoardOutline footprint
  segs = []
  for node in tree:
    if not isinstance(node, list) or not node or node[0] != "footprint":
      continue
    if "BoardOutline" not in node[1]:
      continue
    layer = "F.Cu"
    fx = fy = rot = 0.0
    for sub in node[2:]:
      if isinstance(sub, list) and sub:
        if sub[0] == "layer":
          layer = sub[1]
        elif sub[0] == "at":
          fx = float(sub[1])
          fy = float(sub[2])
          rot = float(sub[3]) if len(sub) > 3 else 0.0
    for sub in node[2:]:
      if not isinstance(sub, list) or not sub or sub[0] not in ("fp_line", "fp_arc"):
        continue
      l_layer = None
      s_pt = m_pt = e_pt = None
      for gsub in sub[1:]:
        if isinstance(gsub, list) and gsub:
          if gsub[0] == "layer":
            l_layer = gsub[1]
          elif gsub[0] == "start":
            s_pt = kicad_local_to_board(fx, fy, rot, layer, float(gsub[1]), float(gsub[2]))
          elif gsub[0] == "mid":
            m_pt = kicad_local_to_board(fx, fy, rot, layer, float(gsub[1]), float(gsub[2]))
          elif gsub[0] == "end":
            e_pt = kicad_local_to_board(fx, fy, rot, layer, float(gsub[1]), float(gsub[2]))
      if l_layer == "Edge.Cuts" and s_pt and e_pt:
        if sub[0] == "fp_arc" and m_pt:
          arc_pts = _sample_3pt_arc(s_pt, m_pt, e_pt, steps=12)
          for i in range(len(arc_pts) - 1):
            segs.append((arc_pts[i], arc_pts[i + 1]))
        else:
          segs.append((s_pt, e_pt))

  if not segs:
    raise RuntimeError("Could not find BoardOutline Edge.Cuts in Saintcon2023.kicad_pcb")

  unused = list(segs)
  outer_loop = [unused[0][0], unused[0][1]]
  unused.pop(0)
  while unused:
    cur = outer_loop[-1]
    best_i = None
    best_rev = False
    best_d = 1e9
    for idx, (a, b) in enumerate(unused):
      da = math.hypot(a[0] - cur[0], a[1] - cur[1])
      db = math.hypot(b[0] - cur[0], b[1] - cur[1])
      if da < best_d:
        best_d = da
        best_i = idx
        best_rev = False
      if db < best_d:
        best_d = db
        best_i = idx
        best_rev = True
    a, b = unused.pop(best_i)
    nxt = a if best_rev else b
    if math.hypot(nxt[0] - outer_loop[0][0], nxt[1] - outer_loop[0][1]) > 1e-3:
      outer_loop.append(nxt)

  # Trace 20 SK6812MINI LEDs starting at U2 (ESP32-S3-WROOM-1) GPIO7 ("LED_DATA")
  gpio7_net, led_chain = trace_ws2812_chain(footprints, "U2", 7, 20)

  # Extract LCD_Displays:LCD_2_8 module (67.0x47.8 mm) and viewing window (58.0x43.8 mm)
  lcd_fp = None
  for fp in footprints.values():
    if "LCD_2_8" in fp["lib"]:
      lcd_fp = fp
      break
  if not lcd_fp:
    raise RuntimeError("Could not find LCD_Displays:LCD_2_8 in Saintcon2023.kicad_pcb")

  silk_rects = [r for r in lcd_fp["rects"] if r["layer"] == "F.SilkS"]
  if len(silk_rects) < 2:
    raise RuntimeError("Expected outer module + inner viewing F.SilkS rects on LCD_2_8")
  silk_rects.sort(key=lambda r: (r["x2"] - r["x1"]) * (r["y2"] - r["y1"]), reverse=True)
  mod_r = silk_rects[0]
  va_r = silk_rects[1]
  mod_mm = (mod_r["x1"], mod_r["y1"], mod_r["x2"] - mod_r["x1"], mod_r["y2"] - mod_r["y1"])
  va_mm = (va_r["x1"], va_r["y1"], va_r["x2"] - va_r["x1"], va_r["y2"] - va_r["y1"])
  va_cx = (va_r["x1"] + va_r["x2"]) / 2.0
  va_cy = (va_r["y1"] + va_r["y2"]) / 2.0
  # 2.8" 320x240 ST7789 active area is 57.60 x 43.20 mm (0.18 mm dot pitch)
  act_w, act_h = 57.60, 43.20
  act_mm = (va_cx - act_w / 2.0, va_cy - act_h / 2.0, act_w, act_h)

  # SW5: 4-way navigation switch + center push (left side, rotated -45 deg for cardinal alignment)
  sw5 = footprints["SW5"]
  sw5_info = {
      "ref": "SW5",
      "kind": "5way",
      "label": "4-Way Joystick + Push",
      "x_mm": round(sw5["x"], 4),
      "y_mm": round(sw5["y"], 4),
      "rot_deg": round(sw5["rot"], 2),
      "radius_mm": 7.5,
  }

  # SW2 (A button, GPIO40/BUTTON_A_PIN) and SW3 (B button, GPIO39/BUTTON_B_PIN) on right side
  sw2 = footprints["SW2"]
  sw2_info = {
      "ref": "SW2",
      "kind": "push",
      "name": "A",
      "bit": 64,
      "gpio": 40,
      "x_mm": round(sw2["x"], 4),
      "y_mm": round(sw2["y"], 4),
      "rot_deg": round(sw2["rot"], 2),
      "radius_mm": 4.5,
  }

  sw3 = footprints["SW3"]
  sw3_info = {
      "ref": "SW3",
      "kind": "push",
      "name": "B",
      "bit": 32,
      "gpio": 39,
      "x_mm": round(sw3["x"], 4),
      "y_mm": round(sw3["y"], 4),
      "rot_deg": round(sw3["rot"], 2),
      "radius_mm": 4.5,
  }

  sw1 = footprints["SW1"]
  sw1_info = {
      "ref": "SW1",
      "x_mm": round(sw1["x"], 4),
      "y_mm": round(sw1["y"], 4),
      "rot_deg": round(sw1["rot"], 2),
      "w_mm": 4.0,
      "h_mm": 9.0,
  }

  # Four minibadge slots in PCA9548A channel order 0..3: U9 (MB1), U10 (MB2), U13 (MB3), U14 (MB4)
  minibadges = []
  for slot_idx, mb_ref in enumerate(("U9", "U10", "U13", "U14")):
    fp = footprints[mb_ref]
    pad_xs = [
        kicad_local_to_board(fp["x"], fp["y"], fp["rot"], fp["layer"], p["lx"], p["ly"])[0]
        for p in fp["pads"]
    ]
    pad_ys = [
        kicad_local_to_board(fp["x"], fp["y"], fp["rot"], fp["layer"], p["lx"], p["ly"])[1]
        for p in fp["pads"]
    ]
    cx = (min(pad_xs) + max(pad_xs)) / 2.0
    cy = (min(pad_ys) + max(pad_ys)) / 2.0
    minibadges.append({
        "slot": slot_idx,
        "ref": mb_ref,
        "x_mm": round(cx, 4),
        "y_mm": round(cy, 4),
        "w_mm": round(max(pad_xs) - min(pad_xs) + 2.54, 4),
        "h_mm": round(max(pad_ys) - min(pad_ys) + 2.54, 4),
        "origin_x_mm": round(fp["x"], 4),
        "origin_y_mm": round(fp["y"], 4),
        "rot_deg": round(fp["rot"], 2),
    })

  standoffs = []
  lanyard_holes = []
  for node in tree:
    if not isinstance(node, list) or not node or node[0] != "footprint":
      continue
    if "Standoff-4-40" in node[1]:
      for sub in node[2:]:
        if isinstance(sub, list) and sub and sub[0] == "at":
          standoffs.append((float(sub[1]), float(sub[2])))
          break
    elif "MountingHole_4mm" in node[1]:
      for sub in node[2:]:
        if isinstance(sub, list) and sub and sub[0] == "at":
          lanyard_holes.append((float(sub[1]), float(sub[2])))
          break

  ir_tx = footprints["D31"]
  ir_rx = footprints["U5"]
  usb_j2 = footprints["J2"]

  return {
      "outer_loop": outer_loop,
      "led_chain": led_chain,
      "gpio7_net": gpio7_net,
      "screen_module_mm": mod_mm,
      "screen_view_mm": va_mm,
      "screen_active_mm": act_mm,
      "sw5": sw5_info,
      "sw2": sw2_info,
      "sw3": sw3_info,
      "sw1": sw1_info,
      "minibadges": minibadges,
      "standoffs_mm": standoffs,
      "lanyard_holes_mm": lanyard_holes,
      "ir_tx": {"ref": "D31", "x_mm": round(ir_tx["x"], 4), "y_mm": round(ir_tx["y"], 4)},
      "ir_rx": {"ref": "U5", "x_mm": round(ir_rx["x"], 4), "y_mm": round(ir_rx["y"], 4)},
      "usb_j2": {"ref": "J2", "x_mm": round(usb_j2["x"], 4), "y_mm": round(usb_j2["y"], 4)},
  }


def ensure_kicad_renders_2023(pcb_src: Path, geom, out_board_png: Path):
  work_dir = BUILD_DIR / "kicad-2023"
  work_dir.mkdir(parents=True, exist_ok=True)
  mod_pcb = work_dir / "Saintcon2023_render.kicad_pcb"
  render_3d_png = work_dir / "render-3d.png"
  fmask_svg = work_dir / "layer-fmask.svg"
  fsilks_sub_svg = work_dir / "layer-fsilks-sub.svg"
  fcu_svg = work_dir / "layer-fcu.svg"

  if not (
      render_3d_png.exists()
      and fmask_svg.exists()
      and fsilks_sub_svg.exists()
      and fcu_svg.exists()
  ):
    raw_txt = pcb_src.read_text(encoding="utf-8", errors="replace")
    mod_txt = raw_txt.replace('(copper_finish "None")', '(copper_finish "ENIG")')
    mod_pcb.write_text(mod_txt, encoding="utf-8")

    uid_gid = f"{os.getuid()}:{os.getgid()}"
    rel_work = work_dir.relative_to(ROOT)
    cmd = (
        f"kicad-cli pcb render --side top --background transparent --quality basic"
        f" -w 2800 -h 2350 -o {rel_work}/render-3d.png {rel_work}/Saintcon2023_render.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.Mask --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o {rel_work}/layer-fmask.svg {rel_work}/Saintcon2023_render.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.SilkS --subtract-soldermask --black-and-white"
        f" --page-size-mode 0 --exclude-drawing-sheet -o {rel_work}/layer-fsilks-sub.svg {rel_work}/Saintcon2023_render.kicad_pcb && "
        f"kicad-cli pcb export svg --layers F.Cu --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o {rel_work}/layer-fcu.svg {rel_work}/Saintcon2023_render.kicad_pcb"
    )
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-u",
            uid_gid,
            "-v",
            f"{ROOT}:/w",
            "-w",
            "/w",
            "kicad/kicad:9.0",
            "sh",
            "-c",
            cmd,
        ],
        check=True,
    )

  im3d = Image.open(render_3d_png).convert("RGBA")
  W_full, H_full = im3d.size
  arr3d = np.array(im3d, dtype=np.float32)
  ys, xs = np.where(arr3d[:, :, 3] > 0)
  xmin_px_full, xmax_px_full = int(xs.min()), int(xs.max())
  ymin_px_full, ymax_px_full = int(ys.min()), int(ys.max())

  oxs = [p[0] for p in geom["outer_loop"]]
  oys = [p[1] for p in geom["outer_loop"]]
  xmin_mm, xmax_mm = min(oxs), max(oxs)
  ymin_mm, ymax_mm = min(oys), max(oys)

  scale_full = (
      (xmax_px_full - xmin_px_full) / (xmax_mm - xmin_mm)
      + (ymax_px_full - ymin_px_full) / (ymax_mm - ymin_mm)
  ) / 2.0
  x0_mm = xmin_mm - xmin_px_full / scale_full
  y0_mm = ymin_mm - ymin_px_full / scale_full
  w_mm = W_full / scale_full
  h_mm = H_full / scale_full

  align_svg_layers(
      work_dir,
      ("layer-fmask", "layer-fsilks-sub", "layer-fcu"),
      W_full,
      H_full,
      x0_mm,
      y0_mm,
      w_mm,
      h_mm,
  )

  board_alpha = arr3d[:, :, 3] / 255.0
  fmask = np.array(Image.open(work_dir / "layer-fmask-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fsilks = np.array(Image.open(work_dir / "layer-fsilks-sub-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fcu = np.array(Image.open(work_dir / "layer-fcu-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0

  lum3d = (0.299 * arr3d[:, :, 0] + 0.587 * arr3d[:, :, 1] + 0.114 * arr3d[:, :, 2]) / 255.0
  rgba = np.zeros((H_full, W_full, 4), dtype=np.float32)

  # Matte charcoal-slate solder mask with subtle copper trace relief, ENIG gold pads,
  # warm amber translucent FR4 windows (the 16 F.Mask diffuser panes around the 4 minibadges),
  # and crisp white silkscreen.
  mask_over_cu = fcu * (1.0 - fmask)
  base_r = 38.0 + mask_over_cu * 11.0 + (lum3d - 0.35) * 16.0
  base_g = 43.0 + mask_over_cu * 12.0 + (lum3d - 0.35) * 16.0
  base_b = 52.0 + mask_over_cu * 14.0 + (lum3d - 0.35) * 18.0

  yy, xx = np.mgrid[0:H_full, 0:W_full]
  tex = np.sin(xx * 0.06 + yy * 0.05) * 2.5 + np.cos(xx * 0.03 - yy * 0.08) * 2.5
  fr4_r = 166.0 + tex
  fr4_g = 132.0 + tex * 0.8
  fr4_b = 68.0 + tex * 0.4

  enig_r = 216.0 + tex * 0.8 + (lum3d - 0.5) * 20.0
  enig_g = 178.0 + tex * 0.6 + (lum3d - 0.5) * 16.0
  enig_b = 72.0 + tex * 0.3 + (lum3d - 0.5) * 10.0

  exposed_r = fr4_r * (1.0 - fcu) + enig_r * fcu
  exposed_g = fr4_g * (1.0 - fcu) + enig_g * fcu
  exposed_b = fr4_b * (1.0 - fcu) + enig_b * fcu

  rgba[:, :, 0] = base_r * (1.0 - fmask) + exposed_r * fmask
  rgba[:, :, 1] = base_g * (1.0 - fmask) + exposed_g * fmask
  rgba[:, :, 2] = base_b * (1.0 - fmask) + exposed_b * fmask

  silk_r, silk_g, silk_b = 232.0, 236.0, 242.0
  rgba[:, :, 0] = rgba[:, :, 0] * (1.0 - fsilks) + silk_r * fsilks
  rgba[:, :, 1] = rgba[:, :, 1] * (1.0 - fsilks) + silk_g * fsilks
  rgba[:, :, 2] = rgba[:, :, 2] * (1.0 - fsilks) + silk_b * fsilks
  rgba[:, :, 3] = board_alpha * 255.0

  pcb_full = Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8), mode="RGBA")

  margin_mm = 2.6
  crop_x0 = max(0, int(round((xmin_mm - margin_mm - x0_mm) * scale_full)))
  crop_y0 = max(0, int(round((ymin_mm - margin_mm - y0_mm) * scale_full)))
  crop_x1 = min(W_full, int(round((xmax_mm + margin_mm - x0_mm) * scale_full)))
  crop_y1 = min(H_full, int(round((ymax_mm + margin_mm - y0_mm) * scale_full)))
  if (crop_x1 - crop_x0) % 2 == 1:
    crop_x1 -= 1
  if (crop_y1 - crop_y0) % 2 == 1:
    crop_y1 -= 1

  pcb_cropped = pcb_full.crop((crop_x0, crop_y0, crop_x1, crop_y1))
  x0_crop_mm = x0_mm + crop_x0 / scale_full
  y0_crop_mm = y0_mm + crop_y0 / scale_full

  W_out = pcb_cropped.width // 2
  H_out = pcb_cropped.height // 2
  scale_out = scale_full / 2.0
  res_out = pcb_cropped.resize((W_out, H_out), Image.Resampling.LANCZOS)
  draw = ImageDraw.Draw(res_out)

  def mm_to_px(xmm, ymm):
    return (xmm - x0_crop_mm) * scale_out, (ymm - y0_crop_mm) * scale_out

  # Subtle PCB outer contour rim highlight so the dark matte board stands out against #0d1117
  edge_pts = [mm_to_px(x, y) for x, y in geom["outer_loop"]]
  if edge_pts:
    draw.line(edge_pts + [edge_pts[0]], fill=(92, 102, 118, 210), width=2)

  # 20x SK6812MINI 3.5x3.5 mm white SMD LED packages
  half_led = 1.65 * scale_out
  lens_r = 1.15 * scale_out
  for led in geom["led_chain"]:
    lx, ly = mm_to_px(led["x_mm"], led["y_mm"])
    draw.rectangle(
        [lx - half_led, ly - half_led, lx + half_led, ly + half_led],
        fill=(228, 232, 236, 240),
        outline=(118, 126, 136, 255),
        width=1,
    )
    draw.ellipse(
        [lx - lens_r, ly - lens_r, lx + lens_r, ly + lens_r],
        fill=(246, 244, 232, 245),
        outline=(180, 184, 190, 255),
        width=1,
    )

  # USB-C receptacle shell (J2 at top-left recess, 101.20, 40.915, rot=180)
  ux, uy = mm_to_px(geom["usb_j2"]["x_mm"], geom["usb_j2"]["y_mm"])
  uw, uh = 4.47 * scale_out, 3.65 * scale_out
  draw.rounded_rectangle(
      [ux - uw, uy - uh, ux + uw, uy + uh],
      radius=3,
      fill=(164, 170, 178, 255),
      outline=(86, 92, 100, 255),
      width=2,
  )

  # Right-angle IR receiver (U5) & IR emitter LED (D31) along top edge
  rx_x, rx_y = mm_to_px(geom["ir_rx"]["x_mm"], geom["ir_rx"]["y_mm"])
  draw.rounded_rectangle(
      [rx_x - 2.6 * scale_out, rx_y - 2.2 * scale_out, rx_x + 2.6 * scale_out, rx_y + 2.0 * scale_out],
      radius=3,
      fill=(28, 26, 34, 250),
      outline=(76, 80, 92, 255),
      width=1,
  )
  tx_x, tx_y = mm_to_px(geom["ir_tx"]["x_mm"], geom["ir_tx"]["y_mm"])
  draw.ellipse(
      [tx_x - 1.6 * scale_out, tx_y - 1.6 * scale_out, tx_x + 1.6 * scale_out, tx_y + 1.6 * scale_out],
      fill=(64, 88, 122, 240),
      outline=(110, 134, 168, 255),
      width=1,
  )

  # 2.8" landscape ST7789 TFT LCD module (covers U2 ESP32-S3 and U4 FPC-40 underneath)
  mx, my, mw, mh = geom["screen_module_mm"]
  vx, vy, vw, vh = geom["screen_view_mm"]
  ax, ay, aw, ah = geom["screen_active_mm"]
  mx0, my0 = mm_to_px(mx, my)
  mx1, my1 = mm_to_px(mx + mw, my + mh)
  draw.rounded_rectangle(
      [mx0, my0, mx1, my1],
      radius=3,
      fill=(24, 27, 33, 255),
      outline=(118, 126, 138, 255),
      width=2,
  )
  vx0, vy0 = mm_to_px(vx, vy)
  vx1, vy1 = mm_to_px(vx + vw, vy + vh)
  draw.rectangle([vx0, vy0, vx1, vy1], fill=(14, 17, 22, 255), outline=(52, 58, 68, 255), width=1)
  ax0, ay0 = mm_to_px(ax, ay)
  ax1, ay1 = mm_to_px(ax + aw, ay + ah)
  draw.rectangle([ax0, ay0, ax1, ay1], fill=(7, 9, 13, 255), outline=(36, 40, 48, 255), width=1)

  # SW5 4-way + center navigation switch on left wing
  jx, jy = mm_to_px(geom["sw5"]["x_mm"], geom["sw5"]["y_mm"])
  j_base_r = 4.6 * scale_out
  j_hat_r = 2.6 * scale_out
  draw.ellipse(
      [jx - j_base_r, jy - j_base_r, jx + j_base_r, jy + j_base_r],
      fill=(188, 194, 202, 235),
      outline=(88, 94, 104, 255),
      width=2,
  )
  draw.ellipse(
      [jx - j_hat_r, jy - j_hat_r, jx + j_hat_r, jy + j_hat_r],
      fill=(34, 38, 44, 255),
      outline=(78, 84, 94, 255),
      width=1,
  )

  # SW2 (A) and SW3 (B) 6x6 mm PTS645 tactile push switches on right wing
  for sw in (geom["sw2"], geom["sw3"]):
    bx, by = mm_to_px(sw["x_mm"], sw["y_mm"])
    half_body = 3.0 * scale_out
    cap_r = 1.85 * scale_out
    draw.rounded_rectangle(
        [bx - half_body, by - half_body, bx + half_body, by + half_body],
        radius=3,
        fill=(196, 202, 210, 240),
        outline=(88, 94, 104, 255),
        width=2,
    )
    draw.ellipse(
        [bx - cap_r, by - cap_r, bx + cap_r, by + cap_r],
        fill=(32, 36, 42, 255),
        outline=(74, 80, 90, 255),
        width=1,
    )

  # SW1 slide power switch on right wing
  sx, sy = mm_to_px(geom["sw1"]["x_mm"], geom["sw1"]["y_mm"])
  sw_w, sw_h = 1.9 * scale_out, 4.3 * scale_out
  draw.rounded_rectangle(
      [sx - sw_w, sy - sw_h, sx + sw_w, sy + sw_h],
      radius=2,
      fill=(166, 172, 180, 245),
      outline=(82, 88, 96, 255),
      width=1,
  )
  draw.rectangle(
      [sx - 1.1 * scale_out, sy - 2.6 * scale_out, sx + 1.1 * scale_out, sy - 0.4 * scale_out],
      fill=(30, 34, 40, 255),
  )

  # Four 4-40 corner standoffs
  for st_x, st_y in geom["standoffs_mm"]:
    px, py = mm_to_px(st_x, st_y)
    outer_r = 2.6 * scale_out
    inner_r = 1.3 * scale_out
    draw.ellipse(
        [px - outer_r, py - outer_r, px + outer_r, py + outer_r],
        fill=(192, 198, 206, 250),
        outline=(92, 98, 108, 255),
        width=2,
    )
    draw.ellipse(
        [px - inner_r, py - inner_r, px + inner_r, py + inner_r],
        fill=(48, 52, 60, 255),
        outline=(118, 124, 134, 255),
        width=1,
    )

  out_board_png.parent.mkdir(parents=True, exist_ok=True)
  res_out.save(out_board_png, optimize=True)

  return {
      "width_px": W_out,
      "height_px": H_out,
      "scale_px_per_mm": round(scale_out, 6),
      "origin_x_mm": round(x0_crop_mm, 6),
      "origin_y_mm": round(y0_crop_mm, 6),
  }, res_out


def build_2023(pcb_override: Path = None):
  work_dir = BUILD_DIR / "kicad-2023"
  work_dir.mkdir(parents=True, exist_ok=True)
  pcb_path = pcb_override if pcb_override else (work_dir / "Saintcon2023.kicad_pcb")

  if not pcb_path.exists():
    kzip = find_2023_kicad_zip_default()
    if not kzip.exists():
      raise FileNotFoundError(f"2023 KiCad zip not found: {kzip}")
    with zipfile.ZipFile(kzip, "r") as zf:
      zf.extract("Saintcon2023.kicad_pcb", work_dir)

  out_dir = BADGES_DIR / "2023"
  layout_path = out_dir / "layout.json"
  board_png_path = out_dir / "board.png"
  overlay_path = BUILD_DIR / "check-overlay-2023.png"

  tree = parse_sexp(pcb_path.read_text(encoding="utf-8", errors="replace"))
  geom = extract_geometry_2023(tree)
  img_meta, board_img = ensure_kicad_renders_2023(pcb_path, geom, board_png_path)

  scale = img_meta["scale_px_per_mm"]
  x0 = img_meta["origin_x_mm"]
  y0 = img_meta["origin_y_mm"]

  def to_px(xmm, ymm):
    return round((xmm - x0) * scale, 2), round((ymm - y0) * scale, 2)

  outer_mm = [[round(x, 4), round(y, 4)] for x, y in geom["outer_loop"]]
  outer_px = [list(to_px(x, y)) for x, y in geom["outer_loop"]]
  oxs = [p[0] for p in outer_mm]
  oys = [p[1] for p in outer_mm]
  pxs = [p[0] for p in outer_px]
  pys = [p[1] for p in outer_px]

  act_x, act_y, act_w, act_h = geom["screen_active_mm"]
  mod_x, mod_y, mod_w, mod_h = geom["screen_module_mm"]
  act_px_x, act_px_y = to_px(act_x, act_y)
  act_px_w = round(act_w * scale, 2)
  act_px_h = round(act_h * scale, 2)
  mod_px_x, mod_px_y = to_px(mod_x, mod_y)
  mod_px_w = round(mod_w * scale, 2)
  mod_px_h = round(mod_h * scale, 2)

  screen_obj = {
      "ref": "U4",
      "part": "LCD-2_8-ST7789",
      "shape": "rect",
      "touch": False,
      "width_dots": 320,
      "height_dots": 240,
      "x_mm": round(act_x, 4),
      "y_mm": round(act_y, 4),
      "w_mm": round(act_w, 4),
      "h_mm": round(act_h, 4),
      "x_px": act_px_x,
      "y_px": act_px_y,
      "w_px": act_px_w,
      "h_px": act_px_h,
      "module_mm": {"x": round(mod_x, 4), "y": round(mod_y, 4), "w": round(mod_w, 4), "h": round(mod_h, 4)},
      "module_px": {"x": mod_px_x, "y": mod_px_y, "w": mod_px_w, "h": mod_px_h},
  }

  for sw in (geom["sw5"], geom["sw2"], geom["sw3"]):
    sw["x_px"], sw["y_px"] = to_px(sw["x_mm"], sw["y_mm"])
    sw["radius_px"] = round(sw["radius_mm"] * scale, 2)

  sw1 = dict(geom["sw1"])
  sw1["x_px"], sw1["y_px"] = to_px(sw1["x_mm"], sw1["y_mm"])
  sw1["w_px"] = round(sw1["w_mm"] * scale, 2)
  sw1["h_px"] = round(sw1["h_mm"] * scale, 2)

  for mb in geom["minibadges"]:
    mb["x_px"], mb["y_px"] = to_px(mb["x_mm"], mb["y_mm"])
    mb["w_px"] = round(mb["w_mm"] * scale, 2)
    mb["h_px"] = round(mb["h_mm"] * scale, 2)

  for led in geom["led_chain"]:
    led["x_px"], led["y_px"] = to_px(led["x_mm"], led["y_mm"])

  ir_tx = dict(geom["ir_tx"])
  ir_tx["x_px"], ir_tx["y_px"] = to_px(ir_tx["x_mm"], ir_tx["y_mm"])
  ir_rx = dict(geom["ir_rx"])
  ir_rx["x_px"], ir_rx["y_px"] = to_px(ir_rx["x_mm"], ir_rx["y_mm"])

  layout = {
      "board_image": {"file": "board.png", **img_meta},
      "outline": {
          "bbox_mm": {"x": round(min(oxs), 4), "y": round(min(oys), 4), "w": round(max(oxs) - min(oxs), 4), "h": round(max(oys) - min(oys), 4)},
          "bbox_px": {"x": round(min(pxs), 2), "y": round(min(pys), 2), "w": round(max(pxs) - min(pxs), 2), "h": round(max(pys) - min(pys), 2)},
          "polygon_mm": outer_mm,
          "polygon_px": outer_px,
      },
      "screen": screen_obj,
      "nav_switch": geom["sw5"],
      "sw5": geom["sw5"],
      "action_buttons": [geom["sw2"], geom["sw3"]],
      "sw2": geom["sw2"],
      "sw3": geom["sw3"],
      "sw1": sw1,
      "ir": {"tx": ir_tx, "rx": ir_rx},
      "minibadges": geom["minibadges"],
      "led_chain_source": {
          "mcu_ref": "U2",
          "gpio": 7,
          "start_net": geom["gpio7_net"],
          "order_refs": [l["ref"] for l in geom["led_chain"]],
      },
      "leds": geom["led_chain"],
  }

  out_dir.mkdir(parents=True, exist_ok=True)
  layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

  overlay = board_img.copy()
  draw = ImageDraw.Draw(overlay)
  poly_pts = [tuple(p) for p in outer_px] + [tuple(outer_px[0])]
  draw.line(poly_pts, fill=(0, 220, 255, 230), width=2)
  draw.rectangle([mod_px_x, mod_px_y, mod_px_x + mod_px_w, mod_px_y + mod_px_h], outline=(255, 200, 0, 220), width=2)
  draw.rectangle([act_px_x, act_px_y, act_px_x + act_px_w, act_px_y + act_px_h], outline=(0, 255, 100, 255), width=3)
  draw.text((act_px_x + 12, act_px_y + 12), "U4 ST7789 2.8\" (320x240)", fill=(0, 255, 110, 255))

  for sw, label, col in (
      (geom["sw5"], "SW5 (4-way + Push)", (255, 110, 0, 255)),
      (geom["sw2"], "SW2 (Btn A)", (0, 210, 100, 255)),
      (geom["sw3"], "SW3 (Btn B)", (60, 150, 255, 255)),
  ):
    r = sw["radius_px"]
    draw.ellipse([sw["x_px"] - r, sw["y_px"] - r, sw["x_px"] + r, sw["y_px"] + r], outline=col, width=3)
    draw.rectangle([sw["x_px"] - 44, sw["y_px"] - r - 18, sw["x_px"] + 62, sw["y_px"] - r - 3], fill=(0, 0, 0, 200))
    draw.text((sw["x_px"] - 41, sw["y_px"] - r - 17), label, fill=col)

  for mb in geom["minibadges"]:
    draw.rectangle(
        [mb["x_px"] - mb["w_px"] / 2, mb["y_px"] - mb["h_px"] / 2, mb["x_px"] + mb["w_px"] / 2, mb["y_px"] + mb["h_px"] / 2],
        outline=(170, 100, 250, 230),
        width=2,
    )

  led_pts = [(l["x_px"], l["y_px"]) for l in geom["led_chain"]]
  draw.line(led_pts, fill=(255, 40, 60, 180), width=2)
  for led in geom["led_chain"]:
    lx, ly = led["x_px"], led["y_px"]
    draw.ellipse([lx - 9, ly - 9, lx + 9, ly + 9], fill=(255, 30, 60, 210), outline=(255, 255, 255, 255), width=2)
    label = f"{led['index']}:{led['ref']}"
    tx = lx + 12 if led["index"] < 10 else lx - 48
    draw.rectangle([tx - 2, ly - 7, tx + 42, ly + 7], fill=(0, 0, 0, 200))
    draw.text((tx, ly - 6), label, fill=(255, 255, 100, 255))

  overlay_path.parent.mkdir(parents=True, exist_ok=True)
  overlay.save(overlay_path)
  print(f"[2023] Wrote {layout_path}, {board_png_path} ({img_meta['width_px']}x{img_meta['height_px']}), {overlay_path}")
  print(
      "[2023] LED chain order:",
      " -> ".join(f"{l['index']}:{l['ref']}({l['din_net']})" for l in geom["led_chain"]),
  )


# ============================================================================
# 2018 (8x32 LED matrix badge) extraction & rendering
# ============================================================================

def extract_geometry_2018(tree):
  footprints, _edge_segments, _top_polys, _top_circles = parse_footprints_and_graphics(tree)

  # Extract the 4 closed Edge.Cuts loops from the Lock artwork module (0.001 mm snap
  # closes sub-micron svg2mod duplicate fragments so KiCad 9 raytraces the positive board).
  lock_fp = None
  for fp in footprints.values():
    if fp["lib"] == "Lock":
      lock_fp = fp
      break
  if not lock_fp:
    raise RuntimeError("Could not find Lock outline module in Saintcon2018.kicad_pcb")

  board_segs = []
  local_segs = []
  for l in lock_fp["lines"]:
    if l["layer"] == "Edge.Cuts":
      ba = (round(l["start"][0], 3), round(l["start"][1], 3))
      bb = (round(l["end"][0], 3), round(l["end"][1], 3))
      if ba != bb:
        board_segs.append((ba, bb))
      la = (round(l["local_start"][0], 3), round(l["local_start"][1], 3))
      lb = (round(l["local_end"][0], 3), round(l["local_end"][1], 3))
      if la != lb:
        local_segs.append((la, lb))

  def build_closed_loops(segs):
    adj = defaultdict(list)
    for idx, (a, b) in enumerate(segs):
      adj[a].append((idx, b))
      adj[b].append((idx, a))
    used = set()
    res = []
    for idx in range(len(segs)):
      if idx in used:
        continue
      used.add(idx)
      a, b = segs[idx]
      pts = [a, b]
      cur = b
      while True:
        nxt = None
        for nidx, nb in adj[cur]:
          if nidx not in used:
            used.add(nidx)
            pts.append(nb)
            cur = nb
            nxt = nb
            break
        if nxt is None:
          break
      if len(pts) > 20 and pts[0] == pts[-1]:
        clean = [pts[0]]
        for p in pts[1:]:
          if p != clean[-1]:
            clean.append(p)
        res.append(clean)
    res.sort(key=len, reverse=True)
    return res

  board_loops = build_closed_loops(board_segs)
  local_loops = build_closed_loops(local_segs)
  if len(board_loops) < 4:
    raise RuntimeError(f"Expected 4 closed Edge.Cuts loops in Lock, found {len(board_loops)}")
  outer_loop = board_loops[0]
  shackle_cutout_loop = board_loops[1]

  # Trace all 256 LEDs (8 rows x 32 columns) directly from U2 (IS31FL3730 @ 0x60)
  # and U3 (IS31FL3730 @ 0x61) C1..C8 / R1..R8 pin nets in Saintcon2018.kicad_pcb.
  c_pins = ["24", "23", "22", "21", "19", "18", "17", "16"]  # C1..C8 on QFN-24
  r_pins = ["7", "8", "9", "10", "12", "13", "14", "15"]    # R1..R8 on QFN-24

  def chip_pin_nets(ref):
    fp = footprints[ref]
    by_num = {p["num"]: p["net_name"] for p in fp["pads"] if p["net_name"]}
    return [by_num[n] for n in c_pins], [by_num[n] for n in r_pins]

  u2_c, u2_r = chip_pin_nets("U2")
  u3_c, u3_r = chip_pin_nets("U3")

  # Map (cathode_net, anode_net) -> LED footprint (pad 1 = cathode K, pad 2 = anode A)
  ka_to_led = {}
  for ref, fp in footprints.items():
    if ref and re.fullmatch(r"D\d+", ref):
      p_by_num = {p["num"]: p["net_name"] for p in fp["pads"]}
      k_net = p_by_num.get("1")
      a_net = p_by_num.get("2")
      if k_net and a_net:
        ka_to_led[(k_net, a_net)] = fp

  # Order LEDs as index = row * 32 + col (row 0 = inner arch bit 0 .. row 7 = outer arch bit 7,
  # col 0..7 = U2 Matrix 1, col 8..15 = U2 Matrix 2, col 16..23 = U3 Matrix 1, col 24..31 = U3 Matrix 2).
  leds = []
  for row in range(8):
    for col in range(32):
      if col < 8:
        k_net, a_net = u2_c[col], u2_r[row]
        driver_ref, i2c_addr, matrix_num = "U2", "0x60", 1
      elif col < 16:
        k_net, a_net = u2_r[row], u2_c[col - 8]
        driver_ref, i2c_addr, matrix_num = "U2", "0x60", 2
      elif col < 24:
        k_net, a_net = u3_c[col - 16], u3_r[row]
        driver_ref, i2c_addr, matrix_num = "U3", "0x61", 1
      else:
        k_net, a_net = u3_r[row], u3_c[col - 24]
        driver_ref, i2c_addr, matrix_num = "U3", "0x61", 2

      fp = ka_to_led.get((k_net, a_net))
      if not fp:
        raise RuntimeError(f"Missing LED for col={col} row={row} (K={k_net}, A={a_net})")
      leds.append({
          "index": len(leds),
          "ref": fp["ref"],
          "col": col,
          "row": row,
          "driver": driver_ref,
          "i2c_addr": i2c_addr,
          "matrix": matrix_num,
          "cathode_net": k_net,
          "anode_net": a_net,
          "din_net": f"{driver_ref}:M{matrix_num} ({k_net}/{a_net})",
          "x_mm": round(fp["x"], 4),
          "y_mm": round(fp["y"], 4),
          "rot_deg": round(fp["rot"], 2),
      })

  if len(leds) != 256:
    raise RuntimeError(f"Expected 256 matrix LEDs, found {len(leds)}")

  # Tactile buttons: SW1 = UP (GPIO 16, bit 1), SW2 = DOWN (GPIO 17, bit 2), SW3 = SELECT (GPIO 18, bit 16)
  btn_specs = [
      ("SW1", "up", "UP", 1, 16),
      ("SW2", "down", "DOWN", 2, 17),
      ("SW3", "center", "SELECT", 16, 18),
  ]
  action_buttons = []
  for ref, bid, bname, bit, gpio in btn_specs:
    fp = footprints[ref]
    action_buttons.append({
        "ref": ref,
        "id": bid,
        "name": bname,
        "bit": bit,
        "gpio": gpio,
        "x_mm": round(fp["x"], 4),
        "y_mm": round(fp["y"], 4),
        "rot_deg": round(fp["rot"], 2),
        "radius_mm": 3.8,
    })

  # 12 Minibadge slots J1..J12 (3 rows x 4 cols)
  minibadges = []
  for idx in range(1, 13):
    ref = f"J{idx}"
    fp = footprints[ref]
    pad_xs = [kicad_local_to_board(fp["x"], fp["y"], fp["rot"], fp["layer"], p["lx"], p["ly"])[0] for p in fp["pads"]]
    pad_ys = [kicad_local_to_board(fp["x"], fp["y"], fp["rot"], fp["layer"], p["lx"], p["ly"])[1] for p in fp["pads"]]
    silks_pts = [p for l in fp["lines"] if l["layer"] == "F.SilkS" for p in (l["start"], l["end"])]
    mxs = [p[0] for p in silks_pts]
    mys = [p[1] for p in silks_pts]
    minibadges.append({
        "slot": idx - 1,
        "ref": ref,
        "x_mm": round((min(mxs) + max(mxs)) / 2.0, 4),
        "y_mm": round((min(mys) + max(mys)) / 2.0, 4),
        "w_mm": round(max(mxs) - min(mxs), 4),
        "h_mm": round(max(mys) - min(mys), 4),
        "pad_x0_mm": round(min(pad_xs), 4),
        "pad_x1_mm": round(max(pad_xs), 4),
        "pad_y_top_mm": round(min(pad_ys), 4),
        "pad_y_bot_mm": round(max(pad_ys), 4),
    })

  # U1 (LOLIN D32), U2/U3 (IS31FL3730), U4/U5 (SOT-23-5)
  u1 = footprints["U1"]
  u1_pad_pts = [kicad_local_to_board(u1["x"], u1["y"], u1["rot"], u1["layer"], p["lx"], p["ly"]) for p in u1["pads"]]
  u1_top_pads = sorted([pt for pt in u1_pad_pts if pt[1] < u1["y"]], key=lambda pt: pt[0])
  u1_bot_pads = sorted([pt for pt in u1_pad_pts if pt[1] > u1["y"]], key=lambda pt: pt[0])

  return {
      "outer_loop": outer_loop,
      "shackle_cutout_loop": shackle_cutout_loop,
      "local_loops": local_loops,
      "leds": leds,
      "action_buttons": action_buttons,
      "minibadges": minibadges,
      "u1_top_pads": u1_top_pads,
      "u1_bot_pads": u1_bot_pads,
      "u2": {"x_mm": footprints["U2"]["x"], "y_mm": footprints["U2"]["y"]},
      "u3": {"x_mm": footprints["U3"]["x"], "y_mm": footprints["U3"]["y"]},
      "u4": {"x_mm": footprints["U4"]["x"], "y_mm": footprints["U4"]["y"]},
      "u5": {"x_mm": footprints["U5"]["x"], "y_mm": footprints["U5"]["y"]},
  }


def ensure_kicad_renders_2018(pcb_path: Path, geom, out_board_png: Path):
  work_dir = BUILD_DIR / "kicad-2018"
  work_dir.mkdir(parents=True, exist_ok=True)

  clean_pcb = work_dir / "render_clean.kicad_pcb"
  render_3d_png = work_dir / "render-3d-clean.png"
  fmask_svg = work_dir / "layer-fmask.svg"
  fsilks_svg = work_dir / "layer-fsilks-sub.svg"
  fcu_svg = work_dir / "layer-fcu.svg"

  if not (render_3d_png.exists() and fmask_svg.exists() and fsilks_svg.exists() and fcu_svg.exists()):
    txt = pcb_path.read_text(encoding="utf-8", errors="replace")
    new_lines = []
    for lp in geom["local_loops"][:4]:
      pts = lp if lp[0] == lp[-1] else (lp + [lp[0]])
      for a, b in zip(pts[:-1], pts[1:]):
        new_lines.append(
            f"    (fp_line (start {a[0]:.3f} {a[1]:.3f}) (end {b[0]:.3f} {b[1]:.3f}) (layer Edge.Cuts) (width 0.15))"
        )
    txt_clean = re.sub(
        r"\s*\(fp_line\s+\(start[^)]+\)\s+\(end[^)]+\)\s+\(layer\s+Edge\.Cuts\)\s+\(width[^)]+\)\)",
        "",
        txt,
    )
    lock_idx = txt_clean.find("(module Lock ")
    insert_at = txt_clean.find("\n", txt_clean.find("(at ", lock_idx)) + 1
    txt_clean = txt_clean[:insert_at] + "\n".join(new_lines) + "\n" + txt_clean[insert_at:]
    clean_pcb.write_text(txt_clean, encoding="utf-8")

    uid_gid = f"{os.getuid()}:{os.getgid()}"
    rel_clean = clean_pcb.relative_to(ROOT)
    cmd = (
        f"kicad-cli pcb render --side top --background transparent --quality basic -w 1800 -h 2800"
        f" -o emulator/build/kicad-2018/render-3d-clean.png {rel_clean} &&"
        f" kicad-cli pcb export svg --layers F.Mask --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o emulator/build/kicad-2018/layer-fmask.svg {rel_clean} &&"
        f" kicad-cli pcb export svg --layers F.SilkS --subtract-soldermask --black-and-white"
        f" --page-size-mode 0 --exclude-drawing-sheet -o emulator/build/kicad-2018/layer-fsilks-sub.svg {rel_clean} &&"
        f" kicad-cli pcb export svg --layers F.Cu --black-and-white --page-size-mode 0"
        f" --exclude-drawing-sheet -o emulator/build/kicad-2018/layer-fcu.svg {rel_clean}"
    )
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-u",
            uid_gid,
            "-v",
            f"{ROOT}:/w",
            "-w",
            "/w",
            "kicad/kicad:9.0",
            "sh",
            "-c",
            cmd,
        ],
        check=True,
    )

  im3d = Image.open(render_3d_png).convert("RGBA")
  W_full, H_full = im3d.size
  arr3d = np.array(im3d, dtype=np.float32)
  ys, xs = np.where(arr3d[:, :, 3] > 0)
  xmin_px_full, xmax_px_full = int(xs.min()), int(xs.max())
  ymin_px_full, ymax_px_full = int(ys.min()), int(ys.max())

  oxs = [p[0] for p in geom["outer_loop"]]
  oys = [p[1] for p in geom["outer_loop"]]
  xmin_mm, xmax_mm = min(oxs), max(oxs)
  ymin_mm, ymax_mm = min(oys), max(oys)

  scale_full = (
      (xmax_px_full - xmin_px_full) / (xmax_mm - xmin_mm)
      + (ymax_px_full - ymin_px_full) / (ymax_mm - ymin_mm)
  ) / 2.0
  x0_mm = xmin_mm - xmin_px_full / scale_full
  y0_mm = ymin_mm - ymin_px_full / scale_full
  w_mm = W_full / scale_full
  h_mm = H_full / scale_full

  align_svg_layers(
      work_dir,
      ("layer-fmask", "layer-fsilks-sub", "layer-fcu"),
      W_full,
      H_full,
      x0_mm,
      y0_mm,
      w_mm,
      h_mm,
  )

  board_alpha = arr3d[:, :, 3] / 255.0
  fmask = np.array(Image.open(work_dir / "layer-fmask-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fsilks = np.array(Image.open(work_dir / "layer-fsilks-sub-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0
  fcu = np.array(Image.open(work_dir / "layer-fcu-aligned.png").convert("RGBA"), dtype=np.float32)[:, :, 3] / 255.0

  lum3d = (0.299 * arr3d[:, :, 0] + 0.587 * arr3d[:, :, 1] + 0.114 * arr3d[:, :, 2]) / 255.0
  rgba = np.zeros((H_full, W_full, 4), dtype=np.float32)

  # Royal/cerulean blue solder mask matching the real 2018 SAINTCON padlock PCB (BadgeBuildSheet.pdf),
  # subtle copper trace relief underneath the mask, ENIG gold exposed pads, and white silkscreen.
  mask_over_cu = fcu * (1.0 - fmask)
  base_r = 20.0 + mask_over_cu * 12.0 + (lum3d - 0.30) * 18.0
  base_g = 90.0 + mask_over_cu * 18.0 + (lum3d - 0.30) * 22.0
  base_b = 144.0 + mask_over_cu * 20.0 + (lum3d - 0.30) * 24.0

  yy, xx = np.mgrid[0:H_full, 0:W_full]
  tex = np.sin(xx * 0.06 + yy * 0.05) * 2.5 + np.cos(xx * 0.03 - yy * 0.08) * 2.5
  fr4_r = 152.0 + tex
  fr4_g = 126.0 + tex * 0.8
  fr4_b = 68.0 + tex * 0.4

  enig_r = 222.0 + tex * 0.8 + (lum3d - 0.5) * 18.0
  enig_g = 182.0 + tex * 0.6 + (lum3d - 0.5) * 14.0
  enig_b = 68.0 + tex * 0.3 + (lum3d - 0.5) * 8.0

  exposed_r = fr4_r * (1.0 - fcu) + enig_r * fcu
  exposed_g = fr4_g * (1.0 - fcu) + enig_g * fcu
  exposed_b = fr4_b * (1.0 - fcu) + enig_b * fcu

  rgba[:, :, 0] = base_r * (1.0 - fmask) + exposed_r * fmask
  rgba[:, :, 1] = base_g * (1.0 - fmask) + exposed_g * fmask
  rgba[:, :, 2] = base_b * (1.0 - fmask) + exposed_b * fmask

  silk_r, silk_g, silk_b = 238.0, 242.0, 248.0
  rgba[:, :, 0] = rgba[:, :, 0] * (1.0 - fsilks) + silk_r * fsilks
  rgba[:, :, 1] = rgba[:, :, 0] * 0.0 + rgba[:, :, 1] * (1.0 - fsilks) + silk_g * fsilks
  rgba[:, :, 2] = rgba[:, :, 2] * (1.0 - fsilks) + silk_b * fsilks
  rgba[:, :, 3] = board_alpha * 255.0

  pcb_full = Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8), mode="RGBA")

  margin_mm = 2.0
  crop_x0 = max(0, int(round((xmin_mm - margin_mm - x0_mm) * scale_full)))
  crop_y0 = max(0, int(round((ymin_mm - margin_mm - y0_mm) * scale_full)))
  crop_x1 = min(W_full, int(round((xmax_mm + margin_mm - x0_mm) * scale_full)))
  crop_y1 = min(H_full, int(round((ymax_mm + margin_mm - y0_mm) * scale_full)))
  if (crop_x1 - crop_x0) % 2 == 1:
    crop_x1 -= 1
  if (crop_y1 - crop_y0) % 2 == 1:
    crop_y1 -= 1

  pcb_cropped = pcb_full.crop((crop_x0, crop_y0, crop_x1, crop_y1))
  x0_crop_mm = x0_mm + crop_x0 / scale_full
  y0_crop_mm = y0_mm + crop_y0 / scale_full

  W_out = pcb_cropped.width // 2
  H_out = pcb_cropped.height // 2
  scale_out = scale_full / 2.0
  res_out = pcb_cropped.resize((W_out, H_out), Image.Resampling.LANCZOS)
  draw = ImageDraw.Draw(res_out)

  def mm_to_px(xmm, ymm):
    return (xmm - x0_crop_mm) * scale_out, (ymm - y0_crop_mm) * scale_out

  # 256x 0603 SMD Red LED packages (oriented radially along the 8 shackle arches)
  led_hw = 0.52 * scale_out
  led_hh = 0.36 * scale_out
  die_r = max(1.1, 0.17 * scale_out)
  for led in geom["leds"]:
    lx, ly = mm_to_px(led["x_mm"], led["y_mm"])
    ang = math.radians(-led["rot_deg"])
    ca, sa = math.cos(ang), math.sin(ang)
    corners = [
        (lx + dx * ca - dy * sa, ly + dx * sa + dy * ca)
        for dx, dy in ((-led_hw, -led_hh), (led_hw, -led_hh), (led_hw, led_hh), (-led_hw, led_hh))
    ]
    draw.polygon(corners, fill=(236, 232, 224, 235), outline=(126, 132, 140, 220))
    draw.ellipse([lx - die_r, ly - die_r, lx + die_r, ly + die_r], fill=(140, 38, 38, 220))

  # Two IS31FL3730 QFN-24 matrix driver ICs (U2 left, U3 right)
  for ic in (geom["u2"], geom["u3"]):
    ix, iy = mm_to_px(ic["x_mm"], ic["y_mm"])
    half_qfn = 1.95 * scale_out
    draw.rounded_rectangle(
        [ix - half_qfn, iy - half_qfn, ix + half_qfn, iy + half_qfn],
        radius=2,
        fill=(26, 29, 34, 250),
        outline=(80, 86, 96, 255),
        width=1,
    )
    draw.ellipse(
        [ix - half_qfn + 2, iy - half_qfn + 2, ix - half_qfn + 5, iy - half_qfn + 5],
        fill=(180, 186, 194, 240),
    )

  # U4 (AP22802AW5-7) & U5 (AP2112K-3.3TRG1) SOT-23-5 packages
  for sot in (geom["u4"], geom["u5"]):
    sx, sy = mm_to_px(sot["x_mm"], sot["y_mm"])
    draw.rectangle(
        [sx - 1.45 * scale_out, sy - 0.85 * scale_out, sx + 1.45 * scale_out, sy + 0.85 * scale_out],
        fill=(28, 31, 36, 248),
        outline=(78, 84, 94, 255),
        width=1,
    )

  # Two 16-pin black female header strips for U1 (LOLIN D32), leaving the SC shield logo visible
  for row_pads in (geom["u1_top_pads"], geom["u1_bot_pads"]):
    if row_pads:
      hx0, hy = mm_to_px(row_pads[0][0] - 1.35, row_pads[0][1])
      hx1, _ = mm_to_px(row_pads[-1][0] + 1.35, row_pads[-1][1])
      hh = 1.25 * scale_out
      draw.rounded_rectangle(
          [hx0, hy - hh, hx1, hy + hh],
          radius=2,
          fill=(24, 27, 32, 248),
          outline=(74, 80, 90, 255),
          width=1,
      )
      hole_r = max(1.5, 0.42 * scale_out)
      for px_mm, py_mm in row_pads:
        px, py = mm_to_px(px_mm, py_mm)
        draw.rectangle(
            [px - hole_r, py - hole_r, px + hole_r, py + hole_r],
            fill=(16, 18, 22, 255),
            outline=(192, 158, 62, 220),
            width=1,
        )

  # Twelve 2x8 female minibadge header pairs (J1..J12) in the 4-column HHC color palette
  # shown on BadgeBuildSheet.pdf Page 1 (Red, Green, Blue, Yellow across columns 1..4).
  col_palettes = [
      ((158, 36, 36, 245), (96, 20, 20, 255)),     # Red
      ((36, 128, 62, 245), (20, 78, 36, 255)),     # Green
      ((34, 86, 168, 245), (18, 48, 102, 255)),    # Blue
      ((186, 146, 32, 245), (116, 88, 16, 255)),   # Yellow
  ]
  for mb in geom["minibadges"]:
    col_idx = mb["slot"] % 4
    hfill, hborder = col_palettes[col_idx]
    hx0, _ = mm_to_px(mb["pad_x0_mm"] - 1.25, mb["pad_y_top_mm"])
    hx1, _ = mm_to_px(mb["pad_x1_mm"] + 1.25, mb["pad_y_top_mm"])
    hh = 1.20 * scale_out
    hole_r = max(1.4, 0.40 * scale_out)
    for py_mm in (mb["pad_y_top_mm"], mb["pad_y_bot_mm"]):
      _, hy = mm_to_px(mb["pad_x0_mm"], py_mm)
      draw.rounded_rectangle([hx0, hy - hh, hx1, hy + hh], radius=2, fill=hfill, outline=hborder, width=1)
      for p_i in range(8):
        px_mm = mb["pad_x0_mm"] + p_i * 2.54
        px, py = mm_to_px(px_mm, py_mm)
        draw.rectangle(
            [px - hole_r, py - hole_r, px + hole_r, py + hole_r],
            fill=(18, 20, 24, 255),
            outline=(200, 166, 68, 215),
            width=1,
        )

  # Three 6x6 mm PTS645 SMD tactile pushbuttons: SW1 (UP), SW2 (DOWN), SW3 (SELECT)
  for sw in geom["action_buttons"]:
    bx, by = mm_to_px(sw["x_mm"], sw["y_mm"])
    half_body = 3.0 * scale_out
    cap_r = 1.85 * scale_out
    draw.rounded_rectangle(
        [bx - half_body, by - half_body, bx + half_body, by + half_body],
        radius=3,
        fill=(196, 202, 210, 242),
        outline=(84, 90, 100, 255),
        width=2,
    )
    draw.ellipse(
        [bx - cap_r, by - cap_r, bx + cap_r, by + cap_r],
        fill=(30, 34, 40, 255),
        outline=(74, 80, 90, 255),
        width=1,
    )

  out_board_png.parent.mkdir(parents=True, exist_ok=True)
  res_out.save(out_board_png, optimize=True)

  return {
      "width_px": W_out,
      "height_px": H_out,
      "scale_px_per_mm": round(scale_out, 6),
      "origin_x_mm": round(x0_crop_mm, 6),
      "origin_y_mm": round(y0_crop_mm, 6),
  }, res_out


def build_2018(pcb_override: Path = None):
  pcb_path = pcb_override if pcb_override else find_2018_pcb_default()
  if not pcb_path.exists():
    raise FileNotFoundError(f"2018 KiCad PCB not found: {pcb_path}")

  out_dir = BADGES_DIR / "2018"
  layout_path = out_dir / "layout.json"
  board_png_path = out_dir / "board.png"
  overlay_path = BUILD_DIR / "check-overlay-2018.png"

  tree = parse_sexp(pcb_path.read_text(encoding="utf-8", errors="replace"))
  geom = extract_geometry_2018(tree)
  img_meta, board_img = ensure_kicad_renders_2018(pcb_path, geom, board_png_path)

  scale = img_meta["scale_px_per_mm"]
  x0 = img_meta["origin_x_mm"]
  y0 = img_meta["origin_y_mm"]

  def to_px(xmm, ymm):
    return round((xmm - x0) * scale, 2), round((ymm - y0) * scale, 2)

  outer_mm = [[round(x, 4), round(y, 4)] for x, y in geom["outer_loop"]]
  outer_px = [list(to_px(x, y)) for x, y in geom["outer_loop"]]
  shackle_mm = [[round(x, 4), round(y, 4)] for x, y in geom["shackle_cutout_loop"]]
  shackle_px = [list(to_px(x, y)) for x, y in geom["shackle_cutout_loop"]]
  oxs = [p[0] for p in outer_mm]
  oys = [p[1] for p in outer_mm]
  pxs = [p[0] for p in outer_px]
  pys = [p[1] for p in outer_px]

  led_radius_px = round(max(3.6, 0.45 * scale), 2)
  for led in geom["leds"]:
    led["x_px"], led["y_px"] = to_px(led["x_mm"], led["y_mm"])
    led["radius_px"] = led_radius_px

  for sw in geom["action_buttons"]:
    sw["x_px"], sw["y_px"] = to_px(sw["x_mm"], sw["y_mm"])
    sw["radius_px"] = round(sw["radius_mm"] * scale, 2)

  for mb in geom["minibadges"]:
    mb["x_px"], mb["y_px"] = to_px(mb["x_mm"], mb["y_mm"])
    mb["w_px"] = round(mb["w_mm"] * scale, 2)
    mb["h_px"] = round(mb["h_mm"] * scale, 2)

  # The 2018 badge has no LCD panel on the PCB: its display IS the 8x32 arched LED matrix
  # on the padlock shackle (hidden=True suppresses #screen-holder on the PCB while
  # #big-screen-canvas renders the 320x96 big 8x32 LED matrix view).
  led_xs = [l["x_px"] for l in geom["leds"]]
  led_ys = [l["y_px"] for l in geom["leds"]]
  screen_obj = {
      "ref": "U2+U3",
      "part": "2x IS31FL3730 8x32 Red LED Matrix",
      "shape": "led_matrix",
      "hidden": True,
      "touch": False,
      "width_dots": 320,
      "height_dots": 96,
      "matrix_cols": 32,
      "matrix_rows": 8,
      "x_px": round(min(led_xs), 2),
      "y_px": round(min(led_ys), 2),
      "w_px": round(max(led_xs) - min(led_xs), 2),
      "h_px": round(max(led_ys) - min(led_ys), 2),
  }

  layout = {
      "board_image": {"file": "board.png", **img_meta},
      "outline": {
          "bbox_mm": {"x": round(min(oxs), 4), "y": round(min(oys), 4), "w": round(max(oxs) - min(oxs), 4), "h": round(max(oys) - min(oys), 4)},
          "bbox_px": {"x": round(min(pxs), 2), "y": round(min(pys), 2), "w": round(max(pxs) - min(pxs), 2), "h": round(max(pys) - min(pys), 2)},
          "polygon_mm": outer_mm,
          "polygon_px": outer_px,
          "shackle_cutout_mm": shackle_mm,
          "shackle_cutout_px": shackle_px,
      },
      "screen": screen_obj,
      "nav_switch": None,
      "action_buttons": geom["action_buttons"],
      "minibadges": geom["minibadges"],
      "led_matrix_source": {
          "mcu_ref": "U1",
          "i2c_scl_gpio": 26,
          "i2c_sda_gpio": 25,
          "sdb_gpio": 27,
          "drivers": [
              {"ref": "U2", "part": "IS31FL3730", "i2c_addr": "0x60", "cols": "0..15"},
              {"ref": "U3", "part": "IS31FL3730", "i2c_addr": "0x61", "cols": "16..31"},
          ],
          "rows": 8,
          "cols": 32,
      },
      "leds": geom["leds"],
  }

  out_dir.mkdir(parents=True, exist_ok=True)
  layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

  overlay = board_img.copy()
  draw = ImageDraw.Draw(overlay)
  draw.line([tuple(p) for p in outer_px] + [tuple(outer_px[0])], fill=(0, 220, 255, 220), width=2)
  draw.line([tuple(p) for p in shackle_px] + [tuple(shackle_px[0])], fill=(0, 220, 255, 200), width=2)

  for led in geom["leds"]:
    lx, ly = led["x_px"], led["y_px"]
    r = led["radius_px"]
    col = (255, 60, 60, 215) if led["col"] < 16 else (255, 150, 40, 215)
    draw.ellipse([lx - r, ly - r, lx + r, ly + r], fill=col, outline=(255, 255, 255, 220), width=1)

  for sw in geom["action_buttons"]:
    r = sw["radius_px"]
    draw.ellipse([sw["x_px"] - r, sw["y_px"] - r, sw["x_px"] + r, sw["y_px"] + r], outline=(0, 255, 120, 255), width=3)
    draw.text((sw["x_px"] - 22, sw["y_px"] - r - 14), f"{sw['ref']} ({sw['name']})", fill=(0, 255, 120, 255))

  for mb in geom["minibadges"]:
    draw.rectangle(
        [mb["x_px"] - mb["w_px"] / 2, mb["y_px"] - mb["h_px"] / 2, mb["x_px"] + mb["w_px"] / 2, mb["y_px"] + mb["h_px"] / 2],
        outline=(190, 120, 255, 220),
        width=2,
    )

  overlay_path.parent.mkdir(parents=True, exist_ok=True)
  overlay.save(overlay_path)
  print(f"[2018] Wrote {layout_path}, {board_png_path} ({img_meta['width_px']}x{img_meta['height_px']}), {overlay_path}")
  print(f"[2018] Verified 256 matrix LEDs ({geom['leds'][0]['ref']}..{geom['leds'][-1]['ref']}) across U2 (0x60) & U3 (0x61)")


def build_layout_and_overlay():
  parser = argparse.ArgumentParser(description="Extract KiCad badge geometry and render board.png + layout.json")
  parser.add_argument("--year", choices=["2018", "2023", "2024", "2025", "2026", "all"], default="2026", help="Badge year to process")
  parser.add_argument("--pcb", type=Path, default=None, help="Path to .kicad_pcb (optional override)")
  args = parser.parse_args()

  if args.year in ("2018", "all"):
    build_2018(args.pcb if args.year == "2018" else None)
  if args.year in ("2023", "all"):
    build_2023(args.pcb if args.year == "2023" else None)
  if args.year in ("2024", "all"):
    build_2024(args.pcb if args.year == "2024" else None)
  if args.year in ("2025", "all"):
    build_2025()
  if args.year in ("2026", "all"):
    pcb26 = args.pcb if (args.pcb and args.year == "2026") else find_2026_pcb_default()
    build_2026(pcb26)


if __name__ == "__main__":
  build_layout_and_overlay()
