#!/usr/bin/env python3
"""Parse EAGLE XML HITBUTT_Minibadge/Base-Rev0.brd and render board.png + layout.json for 2017.

Reads real geometry from emulator/cores/2017/upstream/HITBUTT_Minibadge/Base-Rev0.brd:
  - Layer 20 (Dimension): 20.32 x 20.32 mm board outline
  - Layer 16 (Bottom) & Layer 1 (Top): copper pours, traces, touch polygons/rings
  - Layer 30 (bStop): exposed ring around center hole (10.16, 10.16)
  - Layer 17/18 (Pads/Vias): 16 minibadge through-hole pads, signal vias, center hole via
  - Layer 21/25/51 (tPlace/tNames/tDocu): silkscreen outlines, "You Can't / Touch This",
    pin labels, and SMD packages U1 (ATtiny45 SO08), U2 & U3 (APA102_5050), J1, R1, R2,
    TOUCH_BTN_1 (A3), TOUCH_BTN_2 (A2).
  - Leaves out empty bitmap/logo layers (200..230) as none contain shape data in Base-Rev0.brd.
"""

from pathlib import Path
import json
import math
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BRD = ROOT / "emulator" / "cores" / "2017" / "upstream" / "HITBUTT_Minibadge" / "Base-Rev0.brd"
OUT_DIR = ROOT / "emulator" / "web" / "badges" / "2017"
BUILD_DIR = ROOT / "emulator" / "build"


def _parse_rot(rot_str: str):
  """Return (mirrored: bool, angle_deg: float) from EAGLE rot attribute like 'MR270' or 'R90'."""
  if not rot_str:
    return False, 0.0
  s = rot_str.strip()
  mirrored = False
  if s.startswith("S"):
    s = s[1:]
  if s.startswith("M"):
    mirrored = True
    s = s[1:]
  if s.startswith("R"):
    s = s[1:]
  try:
    ang = float(s)
  except ValueError:
    ang = 0.0
  return mirrored, ang


def _transform_local(lx: float, ly: float, ex: float, ey: float, rot_str: str):
  """Transform package-local (lx, ly) into EAGLE board (x, y) in mm (+Y up)."""
  mirrored, ang_deg = _transform_rot_parts(rot_str)
  x = -lx if mirrored else lx
  y = ly
  rad = math.radians(ang_deg)
  ca, sa = math.cos(rad), math.sin(rad)
  rx = x * ca - y * sa
  ry = x * sa + y * ca
  return ex + rx, ey + ry


def _transform_rot_parts(rot_str: str):
  return _parse_rot(rot_str)


def _load_font(px_size: int, bold: bool = False):
  candidates = [
      "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf" if bold else "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
      "/usr/share/fonts/Adwaita/AdwaitaMono-Bold.ttf" if bold else "/usr/share/fonts/Adwaita/AdwaitaMono-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
  ]
  for c in candidates:
    if Path(c).exists():
      try:
        return ImageFont.truetype(c, px_size)
      except Exception:
        pass
  try:
    return ImageFont.load_default(size=px_size)
  except TypeError:
    return ImageFont.load_default()


def build_2017(brd_path: Path = None):
  brd_file = brd_path if brd_path else DEFAULT_BRD
  if not brd_file.exists():
    raise FileNotFoundError(
        f"2017 EAGLE board file not found: {brd_file} (run emulator/cores/2017/fetch-upstream.sh first)"
    )

  tree = ET.parse(brd_file)
  root = tree.getroot()
  board = root.find("./drawing/board")
  if board is None:
    raise RuntimeError(f"No <drawing><board> found in {brd_file}")

  # Parse packages into a dict keyed by (library_name, package_name)
  packages = {}
  for lib in board.findall("./libraries/library"):
    lib_name = lib.attrib.get("name", "")
    for pkg in lib.findall("./packages/package"):
      pkg_name = pkg.attrib.get("name", "")
      packages[(lib_name, pkg_name)] = pkg

  # Parse plain wires on layer 20 (Dimension) to get board bounding box
  dim_wires = []
  plain_el = board.find("./plain")
  if plain_el is not None:
    for w in plain_el.findall("./wire"):
      if w.attrib.get("layer") == "20":
        dim_wires.append((
            float(w.attrib["x1"]),
            float(w.attrib["y1"]),
            float(w.attrib["x2"]),
            float(w.attrib["y2"]),
        ))

  # Also check package wires on layer 20
  elements_el = board.find("./elements")
  elements = {}
  if elements_el is not None:
    for el in elements_el.findall("./element"):
      name = el.attrib["name"]
      lib_name = el.attrib["library"]
      pkg_name = el.attrib["package"]
      ex = float(el.attrib["x"])
      ey = float(el.attrib["y"])
      rot = el.attrib.get("rot", "R0")
      elements[name] = {
          "name": name,
          "library": lib_name,
          "package": pkg_name,
          "x": ex,
          "y": ey,
          "rot": rot,
          "el_node": el,
          "pkg_node": packages.get((lib_name, pkg_name)),
      }
      pkg_node = packages.get((lib_name, pkg_name))
      if pkg_node is not None:
        for w in pkg_node.findall("./wire"):
          if w.attrib.get("layer") == "20":
            x1, y1 = _transform_local(float(w.attrib["x1"]), float(w.attrib["y1"]), ex, ey, rot)
            x2, y2 = _transform_local(float(w.attrib["x2"]), float(w.attrib["y2"]), ex, ey, rot)
            dim_wires.append((x1, y1, x2, y2))

  if not dim_wires:
    raise RuntimeError("No Dimension (layer 20) outline wires found in Base-Rev0.brd")

  xs_all = [p for w in dim_wires for p in (w[0], w[2])]
  ys_all = [p for w in dim_wires for p in (w[1], w[3])]
  brd_xmin, brd_xmax = min(xs_all), max(xs_all)
  brd_ymin, brd_ymax = min(ys_all), max(ys_all)
  brd_w = brd_xmax - brd_xmin
  brd_h = brd_ymax - brd_ymin

  # Confirm bitmap/logo layers 200..230 have no shape children
  for layer_num in range(200, 231):
    lstr = str(layer_num)
    if plain_el is not None:
      for child in plain_el:
        if child.attrib.get("layer") == lstr:
          raise RuntimeError(f"Unexpected shape on logo layer {lstr} in Base-Rev0.brd")

  # Coordinate convention for top-down image (+X right, +Y down in screen mm):
  # EAGLE board coordinates have +X right (0..20.32) and +Y up (0..20.32).
  # Front view (Top): (x_e, y_e) -> (x_e, brd_ymax - y_e).
  # Back view (Bottom, mirrored horizontally as seen when turning the board over):
  #   (x_e, y_e) -> (back_off_x_mm + (brd_xmax - x_e), brd_ymax - y_e).
  back_off_x_mm = 23.68  # 3.36 mm gap between the two 20.32 mm views -> total 44.00 mm span

  def eagle_to_mm(xe: float, ye: float):
    return xe, brd_ymax - ye

  def eagle_to_back_mm(xe: float, ye: float):
    return back_off_x_mm + (brd_xmax - xe), brd_ymax - ye

  # Render at 2x supersampling then downsample with LANCZOS for crisp anti-aliased geometry
  scale_out = 40.0  # px per mm in final board.png
  margin_x_mm = 2.00
  margin_top_mm = 3.20
  margin_bot_mm = 1.68
  origin_x_mm = brd_xmin - margin_x_mm
  origin_y_mm = 0.0 - margin_top_mm
  span_w_mm = back_off_x_mm + brd_w + 2.0 * margin_x_mm  # 48.00 mm -> 1920 px
  span_h_mm = brd_h + margin_top_mm + margin_bot_mm      # 25.20 mm -> 1008 px

  W_out = int(round(span_w_mm * scale_out))
  H_out = int(round(span_h_mm * scale_out))
  ss = 2
  W_ss = W_out * ss
  H_ss = H_out * ss
  scale_ss = scale_out * ss

  def eagle_to_ss(xe: float, ye: float):
    xmm, ymm = eagle_to_mm(xe, ye)
    return (xmm - origin_x_mm) * scale_ss, (ymm - origin_y_mm) * scale_ss

  def eagle_to_back_ss(xe: float, ye: float):
    xmm, ymm = eagle_to_back_mm(xe, ye)
    return (xmm - origin_x_mm) * scale_ss, (ymm - origin_y_mm) * scale_ss

  def mm_to_px(xmm: float, ymm: float):
    return round((xmm - origin_x_mm) * scale_out, 2), round((ymm - origin_y_mm) * scale_out, 2)

  def mm_to_ss(xmm: float, ymm: float):
    return (xmm - origin_x_mm) * scale_ss, (ymm - origin_y_mm) * scale_ss

  canvas = Image.new("RGBA", (W_ss, H_ss), (0, 0, 0, 0))
  draw = ImageDraw.Draw(canvas)

  # 1. Board substrates for Front (Top) and Back (Bottom, mirrored) (OSH Park 2-layer dark purple FR-4)
  bx0, by0 = eagle_to_ss(brd_xmin, brd_ymax)
  bx1, by1 = eagle_to_ss(brd_xmax, brd_ymin)
  kbx0, kby0 = eagle_to_back_ss(brd_xmax, brd_ymax)
  kbx1, kby1 = eagle_to_back_ss(brd_xmin, brd_ymin)
  corner_r = int(round(0.45 * scale_ss))

  # Subtle drop shadow under both views
  shadow = Image.new("RGBA", (W_ss, H_ss), (0, 0, 0, 0))
  sdraw = ImageDraw.Draw(shadow)
  for rx0, ry0, rx1, ry1 in ((bx0, by0, bx1, by1), (kbx0, kby0, kbx1, kby1)):
    sdraw.rounded_rectangle(
        [rx0 + 6, ry0 + 10, rx1 + 6, ry1 + 10],
        radius=corner_r,
        fill=(0, 0, 0, 130),
    )
  shadow = shadow.filter(ImageFilter.GaussianBlur(radius=10))
  canvas = Image.alpha_composite(canvas, shadow)
  draw = ImageDraw.Draw(canvas)

  for rx0, ry0, rx1, ry1 in ((bx0, by0, bx1, by1), (kbx0, kby0, kbx1, kby1)):
    draw.rounded_rectangle(
        [rx0, ry0, rx1, ry1],
        radius=corner_r,
        fill=(42, 23, 68, 255),
        outline=(88, 56, 128, 255),
        width=max(2, int(round(0.18 * scale_ss))),
    )

  # 2. Parse signals (polygons, copper traces on layers 16 and 1, vias)
  signals_el = board.find("./signals")
  bot_polygons = []
  top_polygons = []
  bot_wires = []
  top_wires = []
  vias = []

  if signals_el is not None:
    for sig in signals_el.findall("./signal"):
      sig_name = sig.attrib.get("name", "")
      for poly in sig.findall("./polygon"):
        layer = poly.attrib.get("layer", "")
        verts = [(float(v.attrib["x"]), float(v.attrib["y"])) for v in poly.findall("./vertex")]
        if len(verts) >= 3:
          if layer == "16":
            bot_polygons.append((sig_name, verts))
          elif layer == "1":
            top_polygons.append((sig_name, verts))
      for w in sig.findall("./wire"):
        layer = w.attrib.get("layer", "")
        if layer in ("1", "16"):
          item = (
              float(w.attrib["x1"]),
              float(w.attrib["y1"]),
              float(w.attrib["x2"]),
              float(w.attrib["y2"]),
              float(w.attrib.get("width", "0.254")),
              sig_name,
          )
          if layer == "16":
            bot_wires.append(item)
          else:
            top_wires.append(item)
      for v in sig.findall("./via"):
        vias.append({
            "x": float(v.attrib["x"]),
            "y": float(v.attrib["y"]),
            "drill": float(v.attrib.get("drill", "0.6")),
            "diameter": float(v.attrib.get("diameter", "1.1")),
            "signal": sig_name,
        })

  # --- FRONT VIEW (Top, layer 1): top copper polygon BTN2 and top copper traces ONLY ---
  for sig_name, verts in top_polygons:
    pts = [eagle_to_ss(x, y) for x, y in verts]
    draw.polygon(pts, fill=(92, 52, 132, 255), outline=(138, 92, 178, 255))

  for x1, y1, x2, y2, w_mm, _sig in top_wires:
    p1 = eagle_to_ss(x1, y1)
    p2 = eagle_to_ss(x2, y2)
    px_w = max(2, int(round(w_mm * scale_ss)))
    draw.line([p1, p2], fill=(108, 66, 152, 255), width=px_w)
    r = px_w / 2.0
    draw.ellipse([p1[0] - r, p1[1] - r, p1[0] + r, p1[1] + r], fill=(108, 66, 152, 255))
    draw.ellipse([p2[0] - r, p2[1] - r, p2[0] + r, p2[1] + r], fill=(108, 66, 152, 255))

  # --- BACK VIEW (Bottom, layer 16 & 30, mirrored): bottom GND pour, bottom traces, Pad 1 ring ---
  for sig_name, verts in bot_polygons:
    pts = [eagle_to_back_ss(x, y) for x, y in verts]
    draw.polygon(pts, fill=(52, 29, 82, 255))

  for x1, y1, x2, y2, w_mm, _sig in bot_wires:
    p1 = eagle_to_back_ss(x1, y1)
    p2 = eagle_to_back_ss(x2, y2)
    px_w = max(2, int(round(w_mm * scale_ss)))
    draw.line([p1, p2], fill=(104, 62, 148, 255), width=px_w)
    r = px_w / 2.0
    draw.ellipse([p1[0] - r, p1[1] - r, p1[0] + r, p1[1] + r], fill=(104, 62, 148, 255))
    draw.ellipse([p2[0] - r, p2[1] - r, p2[0] + r, p2[1] + r], fill=(104, 62, 148, 255))

  # Pad 1 ring on Bottom (Base-Rev0.brd:191-192: circle at (10.16, 10.16) on layer 16 & layer 30 bStop)
  if plain_el is not None:
    for c in plain_el.findall("./circle"):
      layer = c.attrib.get("layer", "")
      cx, cy = float(c.attrib["x"]), float(c.attrib["y"])
      rad = float(c.attrib["radius"])
      w_mm = float(c.attrib.get("width", "0.127"))
      if layer == "16":
        cpx, cpy = eagle_to_back_ss(cx, cy)
        r_outer = (rad + w_mm / 2.0) * scale_ss
        r_inner = max(0.0, (rad - w_mm / 2.0) * scale_ss)
        # Exposed ENIG gold / copper ring on bottom (bStop layer 30 uncovers solder mask over this ring)
        draw.ellipse(
            [cpx - r_outer, cpy - r_outer, cpx + r_outer, cpy + r_outer],
            fill=(218, 178, 72, 245),
            outline=(160, 122, 36, 255),
            width=2,
        )
        if r_inner > 0:
          draw.ellipse([cpx - r_inner, cpy - r_inner, cpx + r_inner, cpy + r_inner], fill=(52, 29, 82, 255))

  # 3a. Draw Top SMD pads (U1, U2, U3) in ENIG gold on the Front view
  for el_name in ("U1", "U2", "U3"):
    el = elements.get(el_name)
    if not el or el["pkg_node"] is None:
      continue
    ex, ey, rot = el["x"], el["y"], el["rot"]
    _mir, ang_deg = _parse_rot(rot)
    for smd in el["pkg_node"].findall("./smd"):
      sx, sy = _transform_local(float(smd.attrib["x"]), float(smd.attrib["y"]), ex, ey, rot)
      dx, dy = float(smd.attrib["dx"]), float(smd.attrib["dy"])
      s_rot = smd.attrib.get("rot", "R0")
      _, s_ang = _parse_rot(s_rot)
      tot_ang = (ang_deg + s_ang) % 360.0
      if int(round(tot_ang)) % 180 == 90:
        dx, dy = dy, dx
      cpx, cpy = eagle_to_ss(sx, sy)
      hw, hh = (dx * scale_ss) / 2.0, (dy * scale_ss) / 2.0
      draw.rounded_rectangle(
          [cpx - hw, cpy - hh, cpx + hw, cpy + hh],
          radius=max(2, int(min(hw, hh) * 0.2)),
          fill=(228, 188, 78, 255),
          outline=(164, 126, 38, 255),
          width=2,
      )

  # 3b. Draw Bottom SMD pads (J1, R1, R2, TOUCH_BTN_1, TOUCH_BTN_2 — all rot="MR...") on the Back view
  for el_name in ("J1", "R1", "R2", "TOUCH_BTN_1", "TOUCH_BTN_2"):
    el = elements.get(el_name)
    if not el or el["pkg_node"] is None:
      continue
    ex, ey, rot = el["x"], el["y"], el["rot"]
    _mir, ang_deg = _parse_rot(rot)
    for smd in el["pkg_node"].findall("./smd"):
      sx, sy = _transform_local(float(smd.attrib["x"]), float(smd.attrib["y"]), ex, ey, rot)
      dx, dy = float(smd.attrib["dx"]), float(smd.attrib["dy"])
      roundness = float(smd.attrib.get("roundness", "0"))
      s_rot = smd.attrib.get("rot", "R0")
      _, s_ang = _parse_rot(s_rot)
      tot_ang = (ang_deg + s_ang) % 360.0
      cpx, cpy = eagle_to_back_ss(sx, sy)
      if roundness >= 90 or el_name.startswith("TOUCH_BTN"):
        r = (max(dx, dy) * scale_ss) / 2.0
        draw.ellipse(
            [cpx - r, cpy - r, cpx + r, cpy + r],
            fill=(232, 192, 82, 255),
            outline=(164, 126, 38, 255),
            width=2,
        )
      else:
        if int(round(tot_ang)) % 180 == 90:
          dx, dy = dy, dx
        hw, hh = (dx * scale_ss) / 2.0, (dy * scale_ss) / 2.0
        draw.rounded_rectangle(
            [cpx - hw, cpy - hh, cpx + hw, cpy + hh],
            radius=max(2, int(min(hw, hh) * 0.2)),
            fill=(228, 188, 78, 255),
            outline=(164, 126, 38, 255),
            width=2,
        )

  # Draw 0805 resistor bodies for R1 and R2 on the Back view
  for r_name in ("R1", "R2"):
    rel = elements.get(r_name)
    if rel:
      rx, ry = eagle_to_back_ss(rel["x"], rel["y"])
      rw, rh = 0.75 * scale_ss, 0.55 * scale_ss
      _mir, rang = _parse_rot(rel["rot"])
      if int(round(rang)) % 180 == 90:
        rw, rh = rh, rw
      draw.rounded_rectangle(
          [rx - rw, ry - rh, rx + rw, ry + rh],
          radius=3,
          fill=(28, 30, 36, 255),
          outline=(90, 96, 108, 255),
          width=2,
      )

  # 4. Draw through-hole pads of U$1 (16 minibadge header pins) on both Front and Back views
  minibadge_pads = []
  u_base = elements.get("U$1")
  if u_base and u_base["pkg_node"] is not None:
    ex, ey, rot = u_base["x"], u_base["y"], u_base["rot"]
    for pad in u_base["pkg_node"].findall("./pad"):
      pname = pad.attrib["name"]
      px, py = _transform_local(float(pad.attrib["x"]), float(pad.attrib["y"]), ex, ey, rot)
      drill = float(pad.attrib.get("drill", "1.016"))
      diam = float(pad.attrib.get("diameter", "1.778"))
      shape = pad.attrib.get("shape", "round")
      ro = (diam / 2.0) * scale_ss
      ri = (drill / 2.0) * scale_ss
      for cpx, cpy in (eagle_to_ss(px, py), eagle_to_back_ss(px, py)):
        if shape == "square":
          draw.rectangle(
              [cpx - ro, cpy - ro, cpx + ro, cpy + ro],
              fill=(232, 192, 82, 255),
              outline=(160, 122, 36, 255),
              width=2,
          )
        else:
          pts = []
          for k in range(8):
            ang = math.radians(22.5 + k * 45.0)
            r_oct = ro / math.cos(math.radians(22.5))
            pts.append((cpx + r_oct * math.cos(ang), cpy + r_oct * math.sin(ang)))
          draw.polygon(pts, fill=(232, 192, 82, 255), outline=(160, 122, 36, 255))
        draw.ellipse(
            [cpx - ri, cpy - ri, cpx + ri, cpy + ri],
            fill=(18, 14, 24, 255),
            outline=(128, 98, 32, 255),
            width=2,
        )
      xmm, ymm = eagle_to_mm(px, py)
      minibadge_pads.append({
          "name": pname,
          "x_mm": round(xmm, 4),
          "y_mm": round(ymm, 4),
          "drill_mm": round(drill, 4),
          "diameter_mm": round(diam, 4),
          "shape": shape,
      })

  # 5. Draw signal vias (including the large center hole via N$5 at (10.16, 10.16)) on both views
  for v in vias:
    ri = (v["drill"] / 2.0) * scale_ss
    ro = max(ri + 0.25 * scale_ss, (v["diameter"] / 2.0) * scale_ss)
    hole_fill = (14, 12, 20, 255) if v["drill"] < 1.5 else (10, 10, 16, 255)
    for cpx, cpy in (eagle_to_ss(v["x"], v["y"]), eagle_to_back_ss(v["x"], v["y"])):
      draw.ellipse(
          [cpx - ro, cpy - ro, cpx + ro, cpy + ro],
          fill=(228, 188, 78, 255),
          outline=(158, 120, 38, 255),
          width=2,
      )
      draw.ellipse(
          [cpx - ri, cpy - ri, cpx + ri, cpy + ri],
          fill=hole_fill,
          outline=(124, 94, 30, 255),
          width=2,
      )

  # 6. Draw package silkscreen wires & circles on Front (non-mirrored) and Back (mirrored)
  silk_col = (238, 240, 246, 245)
  for el in elements.values():
    pkg = el["pkg_node"]
    if pkg is None:
      continue
    ex, ey, rot = el["x"], el["y"], el["rot"]
    mirrored, _ = _parse_rot(rot)
    coord_fn = eagle_to_back_ss if mirrored else eagle_to_ss
    for w in pkg.findall("./wire"):
      if w.attrib.get("layer") in ("21", "51"):
        x1, y1 = _transform_local(float(w.attrib["x1"]), float(w.attrib["y1"]), ex, ey, rot)
        x2, y2 = _transform_local(float(w.attrib["x2"]), float(w.attrib["y2"]), ex, ey, rot)
        p1 = coord_fn(x1, y1)
        p2 = coord_fn(x2, y2)
        w_mm = float(w.attrib.get("width", "0.127"))
        col = silk_col if w.attrib.get("layer") == "21" else (180, 186, 198, 180)
        draw.line([p1, p2], fill=col, width=max(2, int(round(w_mm * scale_ss))))
    for c in pkg.findall("./circle"):
      if c.attrib.get("layer") == "21":
        cx, cy = _transform_local(float(c.attrib["x"]), float(c.attrib["y"]), ex, ey, rot)
        cpx, cpy = coord_fn(cx, cy)
        r = float(c.attrib["radius"]) * scale_ss
        w_mm = float(c.attrib.get("width", "0.127"))
        draw.ellipse(
            [cpx - r, cpy - r, cpx + r, cpy + r],
            outline=silk_col,
            width=max(2, int(round(w_mm * scale_ss))),
        )

  # 7. Render realistic component bodies on Front for U1 (Tiny45-20-SMT SO08) and U2, U3 (APA102 5050 LEDs)
  u1 = elements.get("U1")
  if u1:
    u1x, u1y = eagle_to_ss(u1["x"], u1["y"])
    bw = 2.362 * scale_ss
    bh = 2.55 * scale_ss
    draw.rounded_rectangle(
        [u1x - bw, u1y - bh, u1x + bw, u1y + bh],
        radius=max(3, int(0.25 * scale_ss)),
        fill=(28, 30, 35, 255),
        outline=(74, 80, 92, 255),
        width=2,
    )
    # Pin 1 orientation dot at bottom-left (-1.6, -1.8 in EAGLE local -> bottom-left on screen)
    dpx, dpy = eagle_to_ss(u1["x"] - 1.55, u1["y"] - 1.75)
    dr = 0.32 * scale_ss
    draw.ellipse([dpx - dr, dpy - dr, dpx + dr, dpy + dr], fill=(215, 220, 228, 255))
    # Package value from Base-Rev0.brd:1690: value="Tiny45-20-SMT"
    u1_val = u1["el_node"].attrib.get("value", "Tiny45-20-SMT")
    f_ic = _load_font(int(round(0.50 * scale_ss)), bold=True)
    ic_txt_img = Image.new("RGBA", (int(5.0 * scale_ss), int(1.6 * scale_ss)), (0, 0, 0, 0))
    ic_tdraw = ImageDraw.Draw(ic_txt_img)
    ic_tdraw.text((ic_txt_img.width / 2.0, ic_txt_img.height / 2.0), u1_val, fill=(195, 202, 214, 245), font=f_ic, anchor="mm")
    ic_rot = ic_txt_img.rotate(90, expand=True, resample=Image.Resampling.BICUBIC)
    canvas.alpha_composite(
        ic_rot,
        (int(round(u1x - ic_rot.width / 2.0)), int(round(u1y - ic_rot.height / 2.0))),
    )
    draw = ImageDraw.Draw(canvas)

  for led_ref in ("U2", "U3"):
    led_el = elements.get(led_ref)
    if not led_el:
      continue
    lx_px, ly_px = eagle_to_ss(led_el["x"], led_el["y"])
    half_pkg = 2.45 * scale_ss
    draw.rounded_rectangle(
        [lx_px - half_pkg, ly_px - half_pkg, lx_px + half_pkg, ly_px + half_pkg],
        radius=max(3, int(0.22 * scale_ss)),
        fill=(234, 236, 240, 255),
        outline=(158, 164, 176, 255),
        width=2,
    )
    cav_r = 2.02 * scale_ss
    draw.ellipse(
        [lx_px - cav_r, ly_px - cav_r, lx_px + cav_r, ly_px + cav_r],
        fill=(248, 244, 224, 255),
        outline=(186, 182, 168, 255),
        width=2,
    )
    die_w = 0.52 * scale_ss
    draw.rectangle(
        [lx_px - die_w, ly_px - die_w, lx_px + die_w, ly_px + die_w],
        fill=(52, 56, 66, 230),
    )

  # 8. Draw silkscreen texts on Front (layers 21, 25) and Back (layers 22, 26, mirrored):
  f_ref = _load_font(int(round(1.00 * scale_ss)), bold=True)
  for el_name in ("U1", "U2", "U3"):
    el = elements.get(el_name)
    if not el:
      continue
    for attr in el["el_node"].findall("./attribute"):
      if attr.attrib.get("name") == "NAME" and attr.attrib.get("layer") == "25":
        ax, ay = float(attr.attrib["x"]), float(attr.attrib["y"])
        apx, apy = eagle_to_ss(ax, ay)
        draw.text((apx, apy), el_name, fill=silk_col, font=f_ref, anchor="mm")

  for el_name in ("J1", "R1", "R2"):
    el = elements.get(el_name)
    if not el:
      continue
    for attr in el["el_node"].findall("./attribute"):
      if attr.attrib.get("name") == "NAME" and attr.attrib.get("layer") == "26":
        ax, ay = float(attr.attrib["x"]), float(attr.attrib["y"])
        apx, apy = eagle_to_back_ss(ax, ay)
        draw.text((apx, apy), el_name, fill=silk_col, font=f_ref, anchor="mm")

  if plain_el is not None:
    for txt_node in plain_el.findall("./text"):
      layer = txt_node.attrib.get("layer", "")
      if layer == "21":
        raw_text = (txt_node.text or "").strip()
        tx, ty = float(txt_node.attrib["x"]), float(txt_node.attrib["y"])
        tpx, tpy = eagle_to_ss(tx, ty)
        size_mm = float(txt_node.attrib.get("size", "1.27"))
        f_silk = _load_font(int(round(size_mm * 0.92 * scale_ss)), bold=True)
        txt_img = Image.new("RGBA", (int(10 * scale_ss), int(5 * scale_ss)), (0, 0, 0, 0))
        tdraw = ImageDraw.Draw(txt_img)
        lines = raw_text.splitlines()
        line_step = size_mm * 1.25 * scale_ss
        cy_txt = txt_img.height / 2.0 - ((len(lines) - 1) * line_step) / 2.0
        for idx_l, line in enumerate(lines):
          tdraw.text(
              (txt_img.width / 2.0, cy_txt + idx_l * line_step),
              line,
              fill=silk_col,
              font=f_silk,
              anchor="mm",
          )
        rot_img = txt_img.rotate(90, expand=True, resample=Image.Resampling.BICUBIC)
        canvas.alpha_composite(
            rot_img,
            (int(round(tpx - rot_img.width / 2.0)), int(round(tpy - rot_img.height / 2.0))),
        )
        draw = ImageDraw.Draw(canvas)
      elif layer == "22":
        # Back silkscreen texts (Base-Rev0.brd:193-194, 215-222: 5V, GND, @HITBUTT 2017, compukidmike,
        # d4rkm4tter, kampf, D3M0S, Heinrich, dumby, RuShan).
        # With rot="MR0", in the mirrored back view the origin (tx, ty) is the bottom-left of the text.
        raw_text = (txt_node.text or "").strip()
        tx, ty = float(txt_node.attrib["x"]), float(txt_node.attrib["y"])
        tpx, tpy = eagle_to_back_ss(tx, ty)
        size_mm = float(txt_node.attrib.get("size", "1.27"))
        f_bsilk = _load_font(int(round(size_mm * 0.88 * scale_ss)), bold=True)
        draw.text((tpx, tpy), raw_text, fill=silk_col, font=f_bsilk, anchor="ls")

  # 9. View headers above Front and Back views
  f_hdr = _load_font(int(round(0.58 * scale_ss)), bold=True)
  for cx_mm, title in (
      (brd_w / 2.0, "Front (Top: U1 Tiny45-20-SMT, U2–U3, Pad 2 A2)"),
      (back_off_x_mm + brd_w / 2.0, "Back (Bottom, mirrored: Pad 1 ring A3, TOUCH_BTN_1/2)"),
  ):
    hx, hy = mm_to_ss(cx_mm, -1.55)
    bbox = draw.textbbox((hx, hy), title, font=f_hdr, anchor="mm")
    pad_x, pad_y = int(0.35 * scale_ss), int(0.18 * scale_ss)
    draw.rounded_rectangle(
        [bbox[0] - pad_x, bbox[1] - pad_y, bbox[2] + pad_x, bbox[3] + pad_y],
        radius=int(0.25 * scale_ss),
        fill=(18, 12, 28, 225),
        outline=(110, 76, 152, 210),
        width=2,
    )
    draw.text((hx, hy), title, fill=(236, 232, 246, 255), font=f_hdr, anchor="mm")

  # Downsample 2x supersampled image to final resolution
  board_img = canvas.resize((W_out, H_out), Image.Resampling.LANCZOS)
  OUT_DIR.mkdir(parents=True, exist_ok=True)
  board_png_path = OUT_DIR / "board.png"
  board_img.save(board_png_path, optimize=True)

  # Build layout.json
  outer_mm = [
      [round(brd_xmin, 4), round(0.0, 4)],
      [round(brd_xmax, 4), round(0.0, 4)],
      [round(brd_xmax, 4), round(brd_h, 4)],
      [round(brd_xmin, 4), round(brd_h, 4)],
  ]
  outer_px = [list(mm_to_px(x, y)) for x, y in outer_mm]
  pxs = [p[0] for p in outer_px]
  pys = [p[1] for p in outer_px]

  # LEDs on Front view: index 0 = U2 (10.16, 15.24 in EAGLE -> y_mm = 5.08), index 1 = U3 (10.16, 5.08 in EAGLE -> y_mm = 15.24)
  leds = []
  for idx, ref in enumerate(("U2", "U3")):
    el = elements[ref]
    xmm, ymm = eagle_to_mm(el["x"], el["y"])
    xpx, ypx = mm_to_px(xmm, ymm)
    r_mm = 2.1
    leds.append({
        "index": idx,
        "ref": ref,
        "part": "APA102_5050",
        "x_mm": round(xmm, 4),
        "y_mm": round(ymm, 4),
        "x_px": xpx,
        "y_px": ypx,
        "radius_mm": r_mm,
        "radius_px": round(r_mm * scale_out, 2),
    })

  # Interactive touch pads on the board:
  #   - Bit 1 (value0, ADCTouch.read(A3), threshold > 40):
  #     Pad 1 ring (Base-Rev0.brd:191-192, layer 16 & 30 at (10.16, 10.16)) and TOUCH_BTN_1 (7.62, 9.398, rot="MR0")
  #     are on the Back (Bottom) of the board. Place the interactive zone on the Back view over the Pad 1 ring + TOUCH_BTN_1.
  #   - Bit 2 (value1, ADCTouch.read(A2), threshold > 10):
  #     Top copper polygon BTN2 x=14.478..19.812, y=5.588..14.224 ("You Can't Touch This") on the Front (Top) view,
  #     connected by via (16.764, 9.398) to TOUCH_BTN_2 on the Back view.
  pad0_xmm, pad0_ymm = eagle_to_back_mm(9.50, 10.16)
  pad0_xpx, pad0_ypx = mm_to_px(pad0_xmm, pad0_ymm)
  pad0_r_mm = 3.85
  tb1_xmm, tb1_ymm = eagle_to_back_mm(7.62, 9.398)

  pad1_xmm, pad1_ymm = eagle_to_mm((14.478 + 19.812) / 2.0, (5.588 + 14.224) / 2.0)
  pad1_xpx, pad1_ypx = mm_to_px(pad1_xmm, pad1_ymm)
  pad1_w_mm = 19.812 - 14.478
  pad1_h_mm = 14.224 - 5.588
  pad1_r_mm = round(max(pad1_w_mm, pad1_h_mm) / 2.0, 4)
  tb2_xmm, tb2_ymm = eagle_to_back_mm(16.764, 9.398)

  action_buttons = [
      {
          "ref": "TOUCH_BTN_1",
          "name": "Touch Pad 1 (A3)",
          "label": "Touch Pad 1 (A3 — Back ring & TOUCH_BTN_1)",
          "pin": "A3",
          "view": "back",
          "threshold": 40,
          "bit": 1,
          "min_hit_px": 44,
          "shape": "circle",
          "x_mm": round(pad0_xmm, 4),
          "y_mm": round(pad0_ymm, 4),
          "x_px": pad0_xpx,
          "y_px": pad0_ypx,
          "radius_mm": pad0_r_mm,
          "radius_px": round(pad0_r_mm * scale_out, 2),
          "tp_x_mm": round(tb1_xmm, 4),
          "tp_y_mm": round(tb1_ymm, 4),
      },
      {
          "ref": "TOUCH_BTN_2",
          "name": "Touch Pad 2 (A2)",
          "label": "Touch Pad 2 (A2 — \"You Can't Touch This\")",
          "pin": "A2",
          "view": "front",
          "threshold": 10,
          "bit": 2,
          "min_hit_px": 44,
          "shape": "rect",
          "x_mm": round(pad1_xmm, 4),
          "y_mm": round(pad1_ymm, 4),
          "x_px": pad1_xpx,
          "y_px": pad1_ypx,
          "w_mm": round(pad1_w_mm, 4),
          "h_mm": round(pad1_h_mm, 4),
          "w_px": round(pad1_w_mm * scale_out, 2),
          "h_px": round(pad1_h_mm * scale_out, 2),
          "radius_mm": pad1_r_mm,
          "radius_px": round(pad1_r_mm * scale_out, 2),
          "tp_x_mm": round(tb2_xmm, 4),
          "tp_y_mm": round(tb2_ymm, 4),
      },
  ]

  layout = {
      "board_image": {
          "file": "board.png",
          "width_px": W_out,
          "height_px": H_out,
          "scale_px_per_mm": scale_out,
          "origin_x_mm": round(origin_x_mm, 6),
          "origin_y_mm": round(origin_y_mm, 6),
      },
      "outline": {
          "bbox_mm": {"x": round(brd_xmin, 4), "y": 0.0, "w": round(brd_w, 4), "h": round(brd_h, 4)},
          "bbox_px": {
              "x": round(min(pxs), 2),
              "y": round(min(pys), 2),
              "w": round(max(pxs) - min(pxs), 2),
              "h": round(max(pys) - min(pys), 2),
          },
          "polygon_mm": outer_mm,
          "polygon_px": outer_px,
      },
      "screen": None,
      "nav_switch": None,
      "action_buttons": action_buttons,
      "touch_pads": action_buttons,
      "minibadge_pads": minibadge_pads,
      "leds": leds,
  }

  layout_path = OUT_DIR / "layout.json"
  layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

  # Verification overlay image (emulator/build/check-overlay-2017.png)
  overlay = board_img.copy()
  odraw = ImageDraw.Draw(overlay)
  odraw.line([tuple(p) for p in outer_px] + [tuple(outer_px[0])], fill=(0, 210, 255, 230), width=2)
  f_ov = _load_font(15, bold=True)

  for led in leds:
    lx, ly, lr = led["x_px"], led["y_px"], led["radius_px"]
    odraw.ellipse([lx - lr, ly - lr, lx + lr, ly + lr], outline=(0, 255, 120, 255), width=3)
    lbl = f"LED{led['index']}:{led['ref']}"
    odraw.rectangle([lx - 48, ly - lr - 22, lx + 48, ly - lr - 4], fill=(0, 0, 0, 210))
    odraw.text((lx, ly - lr - 13), lbl, fill=(0, 255, 120, 255), font=f_ov, anchor="mm")

  for btn in action_buttons:
    bx, by, br = btn["x_px"], btn["y_px"], btn["radius_px"]
    col = (255, 120, 40, 255) if btn["bit"] == 1 else (70, 175, 255, 255)
    if btn.get("shape") == "rect":
      hw, hh = btn["w_px"] / 2.0, btn["h_px"] / 2.0
      odraw.rectangle([bx - hw, by - hh, bx + hw, by + hh], outline=col, width=3)
    else:
      odraw.ellipse([bx - br, by - br, bx + br, by + br], outline=col, width=3)
    lbl = f"{btn['ref']} ({btn['pin']} > {btn['threshold']})"
    odraw.rectangle([bx - 92, by + br + 4, bx + 92, by + br + 24], fill=(0, 0, 0, 210))
    odraw.text((bx, by + br + 14), lbl, fill=col, font=f_ov, anchor="mm")

  BUILD_DIR.mkdir(parents=True, exist_ok=True)
  overlay_path = BUILD_DIR / "check-overlay-2017.png"
  overlay.save(overlay_path)
  print(f"[2017] Wrote {layout_path}, {board_png_path} ({W_out}x{H_out}), {overlay_path}")


if __name__ == "__main__":
  build_2017()
