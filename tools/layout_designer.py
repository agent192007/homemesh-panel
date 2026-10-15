#!/usr/bin/env python3
"""
HomeMesh Layout Designer - the desktop authoring application.

The visual half of the PC/ESP32 split. tools/ha_discover.py does the
discovery and owns the output format; this is the authoring surface on
top of it. Pick entities, mark Home-page favourites, drag tiles into the
order you want, and write the layout out.

Deliberately stdlib-only (tkinter). The companion tool is meant to run on
any PC without a toolchain, so a pip install of a GUI framework would
undercut the point. Discovery still needs `websockets`, same as the CLI.

    python tools/layout_designer.py

What the preview is and is not
------------------------------
The canvas mirrors ui/dashboard.cpp exactly: 320x240 landscape, a 28px
header, a 2-column grid of 2 or 3 rows, the same gaps and the same
palette. It is a true preview of geometry, not an impression of one.

It is not a free-form canvas, because the firmware has no free-form
layout to receive. grid_for() in ui/dashboard.cpp derives every tile's
position from its index within its page, so position *is* order. Dragging
edits that order, which is the whole of what the panel can honour. A
pixel-positioning canvas would let you author layouts the panel would
silently ignore.

Home page ordering is derived, not editable, for the same honest reason:
build_pages() walks the entity list once, pushing each favourite onto
page 0 and each entity onto its room page, so the two orders are the same
traversal. Reordering a room reorders Home.
"""

import json
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

try:
    import ha_discover as hd
except SystemExit:
    # ha_discover exits on a missing websockets; say so in GUI terms.
    raise SystemExit("websockets not installed. Run: pip install -r tools/requirements.txt")

# ---------------------------------------------------------------
#  Panel geometry, copied from ui/dashboard.cpp. If these drift the
#  preview lies, so they are named the same as the C++ constants.
# ---------------------------------------------------------------
PANEL_W, PANEL_H = 320, 240
HEADER_H = 28
BODY_H = PANEL_H - HEADER_H
GAP = 8
SCALE = 2  # preview magnification; the panel is too small to author at 1:1

# dashboard.cpp's palette.
COL_BG = "#101418"
COL_TILE = "#2A2F36"
COL_ON = "#F5A623"
COL_ACCENT = "#4C8DFF"
COL_TEXT = "#FFFFFF"
COL_MUTED = "#8A9099"
COL_OK = "#3DDC84"

# Chrome around the preview.
UI_BG = "#F4F5F7"
UI_PANEL = "#FFFFFF"


def grid_for(n):
    """Port of grid_for() in ui/dashboard.cpp. Always 2 columns."""
    r = (n + 1) // 2
    r = max(2, min(3, r))
    w = (PANEL_W - 3 * GAP) // 2
    h = (BODY_H - (r + 1) * GAP) // r
    return w, h, r


def slot_rects(n):
    """Panel-space (x, y, w, h) for each of n tiles, in index order."""
    w, h, _ = grid_for(n)
    out = []
    for k in range(n):
        x = GAP + (k % 2) * (w + GAP)
        y = HEADER_H + GAP + (k // 2) * (h + GAP)
        out.append((x, y, w, h))
    return out


class Designer(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("HomeMesh Layout Designer")
        self.configure(bg=UI_BG)
        # Wide enough for the picker plus a 2x preview (640px) without
        # the preview being clipped - the picker is width-capped below
        # so long entity_ids cannot push it off the edge.
        self.minsize(1240, 780)
        self.geometry("1300x860")

        self.inv = None            # ha_discover.Inventory once connected
        self.include = {}          # entity_id -> tk.BooleanVar
        self.favourite = {}        # entity_id -> tk.BooleanVar
        self.order = []            # entity_ids, grouped by area, page order within
        self.tables = None         # last computed (area_names, rows, acts, sensors)
        self.msgq = queue.Queue()  # worker thread -> UI

        self._build_chrome()
        self._load_saved_selection()
        self.after(100, self._drain)

    # -----------------------------------------------------------
    #  Chrome
    # -----------------------------------------------------------
    def _build_chrome(self):
        top = tk.Frame(self, bg=UI_BG, padx=12, pady=10)
        top.pack(fill="x")

        url, token = hd.resolve_credentials()
        tk.Label(top, text="Home Assistant", bg=UI_BG).pack(side="left")
        self.url_var = tk.StringVar(value=url or "http://localhost:8123")
        tk.Entry(top, textvariable=self.url_var, width=32).pack(side="left", padx=(6, 12))

        tk.Label(top, text="Token", bg=UI_BG).pack(side="left")
        self.token_var = tk.StringVar(value=token or "")
        tk.Entry(top, textvariable=self.token_var, width=24, show="•").pack(
            side="left", padx=(6, 12))

        self.connect_btn = tk.Button(top, text="Connect", width=10, command=self.on_connect)
        self.connect_btn.pack(side="left")

        self.status = tk.Label(top, text="not connected", bg=UI_BG, fg="#777")
        self.status.pack(side="left", padx=12)
        if token:
            self.status.config(text="credentials loaded from tools/.env")

        body = tk.Frame(self, bg=UI_BG, padx=12)
        body.pack(fill="both", expand=True)

        # ---- left: the entity picker
        left = tk.LabelFrame(body, text=" Entities in Home Assistant ",
                             bg=UI_PANEL, padx=8, pady=8, width=470)
        left.pack(side="left", fill="y", expand=False)
        # Without this the frame grows to its widest child - a long
        # entity_id - and shoves the preview off the window.
        left.pack_propagate(False)

        hint = tk.Label(left, bg=UI_PANEL, fg="#666", justify="left", anchor="w",
                        text=("Tick to place on the panel. Star marks the Home page.\n"
                              f"Limits: {hd.MAX_ENTITIES} entities, {hd.MAX_AREAS} rooms, "
                              f"{hd.MAX_PER_PAGE} tiles per page."))
        hint.pack(fill="x", pady=(0, 6))

        canvas_wrap = tk.Frame(left, bg=UI_PANEL)
        canvas_wrap.pack(fill="both", expand=True)
        self.pick_canvas = tk.Canvas(canvas_wrap, bg=UI_PANEL, highlightthickness=0,
                                     width=440, height=600)
        vsb = ttk.Scrollbar(canvas_wrap, orient="vertical", command=self.pick_canvas.yview)
        self.pick_canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        self.pick_canvas.pack(side="left", fill="both", expand=True)

        self.pick_frame = tk.Frame(self.pick_canvas, bg=UI_PANEL)
        self.pick_window = self.pick_canvas.create_window(
            (0, 0), window=self.pick_frame, anchor="nw")
        self.pick_frame.bind(
            "<Configure>",
            lambda e: self.pick_canvas.configure(scrollregion=self.pick_canvas.bbox("all")))
        self.pick_canvas.bind(
            "<Configure>",
            lambda e: self.pick_canvas.itemconfig(self.pick_window, width=e.width))
        self.pick_canvas.bind_all("<MouseWheel>", self._on_wheel)

        tk.Label(self.pick_frame, bg=UI_PANEL, fg="#999",
                 text="Connect to load entities.").pack(pady=20)

        # ---- right: the preview
        right = tk.Frame(body, bg=UI_BG, padx=12)
        right.pack(side="left", fill="both", expand=True)

        self.nb = ttk.Notebook(right)
        self.nb.pack(fill="both", expand=False)
        self.page_canvases = []

        self.note = tk.Label(right, bg=UI_BG, fg="#666", justify="left", anchor="w",
                             text=("Drag a tile to reorder it. A tile's position on the panel is "
                                   "its index within its page,\nso order is the whole of what the "
                                   "firmware can honour. Home order follows the room pages."))
        self.note.pack(fill="x", pady=(8, 0))

        log_frame = tk.LabelFrame(right, text=" Output ", bg=UI_PANEL, padx=6, pady=6)
        log_frame.pack(fill="both", expand=True, pady=(10, 0))
        self.log_text = tk.Text(log_frame, height=9, bg="#1E2227", fg="#D6D9DD",
                                relief="flat", wrap="word", font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True)
        self.log_text.configure(state="disabled")

        # ---- bottom: actions
        bottom = tk.Frame(self, bg=UI_BG, padx=12, pady=10)
        bottom.pack(fill="x")

        self.gen_btn = tk.Button(bottom, text="Write layout", width=16,
                                 state="disabled", command=self.on_generate)
        self.gen_btn.pack(side="left")

        self.push_btn = tk.Button(bottom, text="Push to panel", width=16,
                                  state="disabled", command=self.on_push)
        self.push_btn.pack(side="left", padx=8)
        tk.Label(bottom, bg=UI_BG, fg="#999",
                 text="sends the layout through Home Assistant - the panel stores "
                      "it and restarts into it, no reflash").pack(side="left")

    def _on_wheel(self, event):
        self.pick_canvas.yview_scroll(int(-event.delta / 120), "units")

    def log(self, msg=""):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", str(msg) + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _set_log(self, msg=""):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")
        self.log(msg)

    # -----------------------------------------------------------
    #  Discovery, off the UI thread
    # -----------------------------------------------------------
    def on_connect(self):
        url = self.url_var.get().strip()
        token = self.token_var.get().strip()
        if not url or not token:
            messagebox.showerror("Missing credentials",
                                 "Enter the Home Assistant URL and a long-lived access token.")
            return
        self.connect_btn.config(state="disabled")
        self.status.config(text="connecting...", fg="#777")
        threading.Thread(target=self._fetch, args=(url, token), daemon=True).start()

    def _fetch(self, url, token):
        import asyncio
        try:
            inv = asyncio.run(hd.fetch_inventory(url, token))
            self.msgq.put(("inventory", inv))
        except Exception as e:   # surfaced in the UI, not the console
            self.msgq.put(("error", f"{type(e).__name__}: {e}"))

    def _drain(self):
        try:
            while True:
                kind, payload = self.msgq.get_nowait()
                if kind == "inventory":
                    self.inv = payload
                    self.status.config(
                        text=f"connected - {len(payload.controllable)} controllable, "
                             f"{len(payload.sensors)} sensors", fg="#2A7")
                    self.connect_btn.config(state="normal", text="Reconnect")
                    self.gen_btn.config(state="normal")
                    self.push_btn.config(state="normal")
                    self._populate_picker()
                    self._recompute()
                elif kind == "pushed":
                    self.push_btn.config(state="normal", text="Push to panel")
                    self.log(f"pushed {payload} bytes to the panel")
                    self.log("it stores the layout in NVS and restarts into it; "
                             "give it a few seconds")
                elif kind == "push_failed":
                    self.push_btn.config(state="normal", text="Push to panel")
                    self.log(f"PUSH FAILED {payload}")
                    messagebox.showerror("Push failed", payload)
                elif kind == "error":
                    self.status.config(text="connection failed", fg="#C33")
                    self.connect_btn.config(state="normal")
                    self.log(f"ERROR {payload}")
                    messagebox.showerror("Could not reach Home Assistant", payload)
        except queue.Empty:
            pass
        self.after(100, self._drain)

    # -----------------------------------------------------------
    #  Picker
    # -----------------------------------------------------------
    def _populate_picker(self):
        for w in self.pick_frame.winfo_children():
            w.destroy()

        saved_inc = {e for e, v in self.include.items() if v.get()} or None
        saved_fav = {e for e, v in self.favourite.items() if v.get()}
        self.include, self.favourite = {}, {}

        by_area = {}
        for eid, name, kind, area in self.inv.controllable:
            by_area.setdefault(area, []).append((eid, name, kind))

        # Unassigned last: they cannot be placed, so they are shown
        # greyed rather than hidden - "where did my light go" is a
        # worse failure than a disabled row.
        keys = sorted((a for a in by_area if a is not None),
                      key=lambda a: self.inv.area_of(a))
        if None in by_area:
            keys.append(None)

        for area in keys:
            placeable = area is not None
            hdr = tk.Frame(self.pick_frame, bg=UI_PANEL)
            hdr.pack(fill="x", pady=(10, 2))
            tk.Label(hdr, text=self.inv.area_of(area), bg=UI_PANEL,
                     fg="#222" if placeable else "#999",
                     font=("Segoe UI", 10, "bold")).pack(side="left")
            if not placeable:
                tk.Label(hdr, text="  no area in HA - cannot be placed",
                         bg=UI_PANEL, fg="#B00").pack(side="left")

            for eid, name, kind in by_area[area]:
                row = tk.Frame(self.pick_frame, bg=UI_PANEL)
                row.pack(fill="x")

                inc = tk.BooleanVar(value=placeable and (saved_inc is None or eid in saved_inc))
                fav = tk.BooleanVar(value=eid in saved_fav)
                self.include[eid] = inc
                self.favourite[eid] = fav

                cb = tk.Checkbutton(row, variable=inc, bg=UI_PANEL,
                                    command=self._on_pick_change,
                                    state="normal" if placeable else "disabled")
                cb.pack(side="left")
                star = tk.Checkbutton(row, variable=fav, bg=UI_PANEL, text="★",
                                      fg=COL_ON, selectcolor=UI_PANEL,
                                      command=self._on_pick_change,
                                      state="normal" if placeable else "disabled")
                star.pack(side="left")
                tk.Label(row, text=name, bg=UI_PANEL, width=24, anchor="w",
                         fg="#222" if placeable else "#AAA").pack(side="left")
                tk.Label(row, text=kind, bg=UI_PANEL, width=9, anchor="w",
                         fg=COL_ACCENT if kind != "Toggle" else "#888").pack(side="left")
                tk.Label(row, text=eid, bg=UI_PANEL, fg="#999", width=20, anchor="w",
                         font=("Consolas", 8)).pack(side="left")

        self.pick_canvas.yview_moveto(0)

    def _on_pick_change(self):
        # A newly ticked entity joins the end of its area's block; an
        # unticked one leaves. Everything already placed keeps the
        # order it was dragged into.
        self._recompute()

    # -----------------------------------------------------------
    #  Selection -> tables -> preview
    # -----------------------------------------------------------
    def _selected(self):
        return {e for e, v in self.include.items() if v.get()}

    def _recompute(self):
        if not self.inv:
            return
        sel = self._selected()
        if not sel:
            # Nothing ticked is a legitimate state to be in while
            # picking, not an error. Empty tables render empty pages.
            self.tables = ([], [], [], [])
            self._set_log("nothing selected")
            self._rebuild_pages()
            return

        # Keep the dragged order for anything still selected, append
        # the rest in inventory order.
        kept = [e for e in self.order if e in sel]
        new = [r[0] for r in self.inv.controllable if r[0] in sel and r[0] not in kept]
        self.order = kept + new

        lines = []
        try:
            self.tables = hd.select_layout(
                self.inv,
                include=sel,
                favourite={e for e, v in self.favourite.items() if v.get()},
                # None means "whatever HA has", matching the CLI with no
                # --automations, so both authoring paths emit the same
                # layout from the same selection.
                automations=None,
                order=self.order,
                log=lines.append)
        except ValueError as e:
            self.log(f"ERROR {e}")
            return

        # Rewrite self.order to exactly what survived selection, grouped
        # by area. Grouping is what keeps the room pages in a stable
        # sequence: select_layout assigns area indices in first-seen
        # order, so an interleaved list would reshuffle the pages.
        _, rows, _, _ = self.tables
        self.order = [r[0] for r in sorted(rows, key=lambda r: r[3])]

        area_names, rows, acts, sensors = self.tables
        payload = hd.dump_layout(hd.build_layout(area_names, rows, acts, sensors))
        self._set_log(
            f"{len(rows)} entities across {len(area_names)} rooms, "
            f"{sum(1 for r in rows if r[4])} on Home, {len(sensors)} sensors, "
            f"{len(acts)} automations")
        self.log(f"layout payload: {len(payload)} bytes")
        for line in lines:
            self.log(line)

        self._rebuild_pages()

    def _page_entities(self, page):
        """Port of build_pages(): page 0 is favourites, page k is area k-1."""
        _, rows, _, _ = self.tables
        out = []
        for r in rows:
            if page == 0 and r[4]:
                out.append(r)
            elif page > 0 and r[3] == page - 1:
                out.append(r)
        return out[:hd.MAX_PER_PAGE]

    def _rebuild_pages(self):
        keep = self.nb.index(self.nb.select()) if self.nb.tabs() else 0
        for tab in self.nb.tabs():
            self.nb.forget(tab)
        self.page_canvases = []

        area_names, _, _, _ = self.tables
        titles = ["Home"] + list(area_names)
        for page, title in enumerate(titles):
            frame = tk.Frame(self.nb, bg=UI_BG, padx=10, pady=10)
            cv = tk.Canvas(frame, width=PANEL_W * SCALE, height=PANEL_H * SCALE,
                           bg=COL_BG, highlightthickness=1, highlightbackground="#999")
            cv.pack()
            self.nb.add(frame, text=title)
            self.page_canvases.append(cv)
            self._draw_page(cv, page, title)

        if self.nb.tabs():
            self.nb.select(min(keep, len(self.nb.tabs()) - 1))

    def _draw_page(self, cv, page, title):
        cv.delete("all")
        s = SCALE
        ents = self._page_entities(page)

        # Header, as ui/dashboard.cpp draws it.
        cv.create_rectangle(0, 0, PANEL_W * s, HEADER_H * s,
                            fill="#171C21", outline="")
        cv.create_text(GAP * s, HEADER_H * s / 2, text=title, anchor="w",
                       fill=COL_TEXT, font=("Segoe UI", 9 * s // 2 + 4))
        cv.create_oval(PANEL_W * s - GAP * s - 14, HEADER_H * s / 2 - 7,
                       PANEL_W * s - GAP * s, HEADER_H * s / 2 + 7,
                       fill=COL_OK, outline="")
        # Sensor slots are drawn as placeholders, never as numbers. A
        # plausible-looking 21.5 degrees in a design tool is the same
        # mistake as inventing template sensors for the rooms that have
        # none: it reads as data and isn't.
        sensors = [r for r in self.tables[3] if page > 0 and r[1] == page - 1]
        if sensors:
            slots = "  ".join("--.-°" if not r[2] else "--%" for r in sensors)
            cv.create_text(PANEL_W * s - 34 * s, HEADER_H * s / 2,
                           text=slots, anchor="e", fill=COL_MUTED,
                           font=("Segoe UI", 8))

        if not ents:
            cv.create_text(PANEL_W * s / 2, PANEL_H * s / 2, text="Nothing here",
                           fill=COL_MUTED, font=("Segoe UI", 11))
            self._bind_drag(cv, page, [])
            return

        rects = slot_rects(len(ents))
        for k, (ent, (x, y, w, h)) in enumerate(zip(ents, rects)):
            eid, name, kind, area, fav = ent
            tag = f"slot{k}"
            cv.create_rectangle(x * s, y * s, (x + w) * s, (y + h) * s,
                                fill=COL_TILE, outline="#3A4047", width=1,
                                tags=("tile", tag))
            cv.create_text((x + 10) * s, (y + 14) * s, text=name, anchor="w",
                           fill=COL_TEXT, font=("Segoe UI", 9), width=(w - 16) * s,
                           tags=("tile", tag))
            cv.create_text((x + 10) * s, (y + h - 14) * s, text="Off", anchor="w",
                           fill=COL_MUTED, font=("Segoe UI", 8), tags=("tile", tag))
            if fav:
                cv.create_text((x + w - 10) * s, (y + 12) * s, text="★",
                               anchor="e", fill=COL_ON, font=("Segoe UI", 9),
                               tags=("tile", tag))
            if kind != "Toggle":
                # The mini brightness bar, gated on kind != Toggle in
                # dashboard.cpp - the slider is the visible consequence
                # of classify() picking RGB or Dimmable.
                by = (y + h - 7) * s
                cv.create_rectangle((x + 10) * s, by - 3, (x + w - 10) * s, by + 3,
                                    fill="#3A4047", outline="", tags=("tile", tag))
                cv.create_rectangle((x + 10) * s, by - 3,
                                    (x + 10) * s + ((w - 20) * s) * 0.6, by + 3,
                                    fill=COL_ACCENT, outline="", tags=("tile", tag))

        self._bind_drag(cv, page, rects)

    # -----------------------------------------------------------
    #  Drag to reorder
    # -----------------------------------------------------------
    def _bind_drag(self, cv, page, rects):
        # Home is derived from the room pages (see build_pages), so
        # there is no independent order to edit there.
        if page == 0 or len(rects) < 2:
            cv.unbind("<ButtonPress-1>")
            cv.unbind("<B1-Motion>")
            cv.unbind("<ButtonRelease-1>")
            return

        state = {"from": None, "x": 0, "y": 0}

        def hit(ev):
            s = SCALE
            for k, (x, y, w, h) in enumerate(rects):
                if x * s <= ev.x <= (x + w) * s and y * s <= ev.y <= (y + h) * s:
                    return k
            return None

        def press(ev):
            k = hit(ev)
            if k is None:
                return
            state.update({"from": k, "x": ev.x, "y": ev.y})
            cv.tag_raise(f"slot{k}")

        def motion(ev):
            if state["from"] is None:
                return
            cv.move(f"slot{state['from']}", ev.x - state["x"], ev.y - state["y"])
            state.update({"x": ev.x, "y": ev.y})

        def release(ev):
            k = state["from"]
            state["from"] = None
            if k is None:
                return
            target = hit(ev)
            if target is None or target == k:
                self._draw_page(cv, page, self.nb.tab(page, "text"))
                return
            self._reorder(page, k, target)

        cv.bind("<ButtonPress-1>", press)
        cv.bind("<B1-Motion>", motion)
        cv.bind("<ButtonRelease-1>", release)

    def _reorder(self, page, src, dst):
        """Move tile src to slot dst within a room page.

        The page's entities form a contiguous block in self.order
        (grouped by area in _recompute), so this permutes that block
        and leaves every other page untouched.
        """
        page_ids = [r[0] for r in self._page_entities(page)]
        if not (0 <= src < len(page_ids)) or not (0 <= dst < len(page_ids)):
            return
        moved = page_ids.pop(src)
        page_ids.insert(dst, moved)

        block = [i for i, e in enumerate(self.order) if e in set(page_ids)]
        for slot, idx in enumerate(block):
            self.order[idx] = page_ids[slot]
        self._recompute()

    # -----------------------------------------------------------
    #  Output
    # -----------------------------------------------------------
    def _load_saved_selection(self):
        """Reopen with the layout that is currently on disk, so the app
        edits the live layout rather than starting blank every time."""
        f = hd.LAYOUT_OUT
        if not f.exists():
            return
        try:
            saved = json.loads(f.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return
        if saved.get("v") != hd.LAYOUT_VERSION:
            return
        ents = saved.get("entities", [])
        self.order = [e["id"] for e in ents]
        for e in ents:
            self.include[e["id"]] = tk.BooleanVar(value=True)
            self.favourite[e["id"]] = tk.BooleanVar(value=bool(e.get("fav")))
        self.log(f"loaded {len(ents)} entities from {f.name}")

    def on_push(self):
        """Send the layout to the panel through Home Assistant.

        Nothing here addresses the panel. The firmware subscribes to
        LAYOUT_EVENT on the WebSocket it already holds, so a push only
        has to reach HA - which is the one address both ends already
        agree on.
        """
        if not self.tables or not self.tables[1]:
            messagebox.showwarning("Nothing to push",
                                   "Tick at least one entity first.")
            return
        layout = hd.build_layout(*self.tables)
        self.push_btn.config(state="disabled", text="Pushing...")
        threading.Thread(target=self._push, args=(layout,), daemon=True).start()

    def _push(self, layout):
        import asyncio
        try:
            n = asyncio.run(hd.push_layout(self.url_var.get().strip(),
                                           self.token_var.get().strip(), layout))
            self.msgq.put(("pushed", n))
        except Exception as e:
            self.msgq.put(("push_failed", f"{type(e).__name__}: {e}"))

    def on_generate(self):
        if not self.tables:
            return
        area_names, rows, acts, sensors = self.tables
        if not rows:
            messagebox.showwarning("Nothing selected",
                                   "Tick at least one entity before writing a layout.")
            return
        lines = []
        hd.write_outputs(area_names, rows, acts, sensors, log=lines.append)
        for line in lines:
            self.log(line)
        self.log("rebuild and reflash env:ha to put this on the panel")


if __name__ == "__main__":
    Designer().mainloop()
