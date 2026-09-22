import os
import time
import ctypes
import tkinter as tk
from tkinter import filedialog
from ctypes import wintypes

# --- Constants (Matching C++ ui namespace) ---
kClientW = 340
kClientH = 530
kBarW = 260
kSeekBar = (40, 318, 300, 324)
kSeekHit = (30, 308, 310, 334)
kOpenBtn = (40, 460, 300, 500)
kPlayCtr = (170, 390)
kPlayR = 35

# --- State Variables ---
file_name = "No file selected"
is_playing = False
dragging = False
duration_ms = 0
position_ms = 0
play_start_tick = 0

# --- MCI Setup via ctypes ---
winmm = ctypes.windll.winmm
mciSendStringW = winmm.mciSendStringW
mciSendStringW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint, wintypes.HANDLE]
mciSendStringW.restype = wintypes.DWORD

def mci(cmd, return_len=0):
    buf = ctypes.create_unicode_buffer(return_len) if return_len > 0 else None
    err = mciSendStringW(cmd, buf, return_len, None)
    return (err == 0), (buf.value if buf else "")

# --- Helper Functions ---
def in_rect(r, x, y):
    return r[0] <= x <= r[2] and r[1] <= y <= r[3]

def format_time(ms):
    total_sec = ms // 1000
    h = total_sec // 3600
    m = (total_sec // 60) % 60
    s = total_sec % 60
    if h > 0:
        return f"{h}:{m:02d}:{s:02d}"
    return f"{m}:{s:02d}"

def pos_from_x(x):
    t = x - kSeekBar[0]
    t = max(0, min(t, kBarW))
    ms = int(t * duration_ms / kBarW) if duration_ms > 0 else 0
    if duration_ms > 0 and ms >= duration_ms:
        ms = duration_ms - 1
    return ms

def round_rect(canvas, x1, y1, x2, y2, r, **kwargs):
    """Helper to draw rounded rectangles on Tkinter Canvas"""
    if 'outline' not in kwargs:
        kwargs['outline'] = ''
    canvas.create_oval(x1, y1, x1+2*r, y1+2*r, **kwargs)
    canvas.create_oval(x2-2*r, y1, x2, y1+2*r, **kwargs)
    canvas.create_oval(x1, y2-2*r, x1+2*r, y2, **kwargs)
    canvas.create_oval(x2-2*r, y2-2*r, x2, y2, **kwargs)
    canvas.create_rectangle(x1+r, y1, x2-r, y2, **kwargs)
    canvas.create_rectangle(x1, y1+r, x2, y2-r, **kwargs)

# --- Audio Controls ---
def seek_to(ms):
    global position_ms, play_start_tick
    if duration_ms == 0: 
        return
    position_ms = ms
    if is_playing:
        mci(f"play music from {ms}")
        play_start_tick = time.time() * 1000
    else:
        mci(f"seek music to {ms}")

def open_audio_file():
    global is_playing, duration_ms, position_ms, file_name, play_start_tick
    path = filedialog.askopenfilename(
        title="Select Audio File",
        filetypes=[("WMA Files", "*.wma"), ("Audio Files", "*.wma;*.mp3;*.wav"), ("All Files", "*.*")]
    )
    if not path:
        return

    mci("close music")
    is_playing = False
    duration_ms = 0
    position_ms = 0

    # Format path for MCI
    p = path.replace('/', '\\')
    opened, _ = mci(f'open "{p}" alias music')
    if not opened:
        opened, _ = mci(f'open "{p}" type mpegvideo alias music')

    if not opened:
        file_name = "Error opening file"
        on_paint()
        return

    mci("set music time format milliseconds")
    success, buf = mci("status music length", 128)
    if success and buf:
        duration_ms = int(buf)

    file_name = os.path.basename(path)

    if duration_ms > 0:
        success, _ = mci("play music")
        if success:
            is_playing = True
            play_start_tick = time.time() * 1000
    on_paint()

def toggle_play():
    global is_playing, play_start_tick
    if duration_ms == 0:
        return
    if is_playing:
        mci("pause music")
        is_playing = False
    else:
        success, _ = mci("play music")
        if success:
            is_playing = True
            play_start_tick = time.time() * 1000
    on_paint()

# --- Drawing Routine ---
def on_paint():
    canvas.delete("all")
    
    # Base background (RGB 18, 18, 18)
    canvas.configure(bg="#121212")
    
    # Outer rounded rect (RGB 36, 36, 45) -> radius 20 (matching C++ w/h 40)
    round_rect(canvas, 10, 10, kClientW - 10, kClientH - 10, r=20, fill="#24242d")

    # Abstract background circles
    canvas.create_oval(60, 60, 280, 280, fill="#0096ff", outline="")
    canvas.create_oval(140, 140, 200, 200, fill="#24242f", outline="")

    # Title Text
    canvas.create_text(kClientW // 2, 35, text="WMA Player", fill="#e0e0e0", 
                       font=("Segoe UI", 16, "bold"), anchor="center")

    # Track Name
    canvas.create_text(kClientW // 2, 300, text=file_name, fill="#b3b3b3", 
                       font=("Segoe UI", 12), anchor="center")

    # Seek Bar Track
    round_rect(canvas, kSeekBar[0], kSeekBar[1], kSeekBar[2], kSeekBar[3], r=3, fill="#3a3a48")

    # Seek Bar Progress
    prog_w = int(position_ms * kBarW / duration_ms) if duration_ms > 0 else 0
    prog_w = max(0, min(prog_w, kBarW))
    
    if prog_w > 0:
        round_rect(canvas, kSeekBar[0], kSeekBar[1], kSeekBar[0] + prog_w, kSeekBar[3], r=3, fill="#00c6ff")
    
    cy = (kSeekBar[1] + kSeekBar[3]) // 2
    cx = kSeekBar[0] + prog_w
    canvas.create_oval(cx - 6, cy - 6, cx + 6, cy + 6, fill="#00c6ff", outline="")

    # Timers
    canvas.create_text(kSeekBar[0], 345, text=format_time(position_ms), 
                       fill="#888888", font=("Segoe UI", 10), anchor="w")
    canvas.create_text(kSeekBar[2], 345, text=format_time(duration_ms), 
                       fill="#888888", font=("Segoe UI", 10), anchor="e")

    # Play Button Circle
    c_x, c_y = kPlayCtr
    canvas.create_oval(c_x - kPlayR, c_y - kPlayR, c_x + kPlayR, c_y + kPlayR, fill="#ffffff", outline="")

    # Play/Pause Icons
    if not is_playing:
        canvas.create_polygon(c_x - 8, c_y - 15, c_x - 8, c_y + 15, c_x + 15, c_y, fill="#121212")
    else:
        canvas.create_rectangle(c_x - 10, c_y - 15, c_x - 4, c_y + 15, fill="#121212", outline="")
        canvas.create_rectangle(c_x + 4, c_y - 15, c_x + 10, c_y + 15, fill="#121212", outline="")

    # Open Button
    round_rect(canvas, kOpenBtn[0], kOpenBtn[1], kOpenBtn[2], kOpenBtn[3], r=15, fill="#3c3c4b")
    canvas.create_text((kOpenBtn[0] + kOpenBtn[2]) // 2, (kOpenBtn[1] + kOpenBtn[3]) // 2,
                       text="Choose WMA File", fill="#dcdcdc", font=("Segoe UI", 12))

# --- Events and Timer ---
def on_mouse_down(event):
    global dragging, position_ms
    x, y = event.x, event.y
    dx = x - kPlayCtr[0]
    dy = y - kPlayCtr[1]

    if dx * dx + dy * dy <= kPlayR * kPlayR:
        toggle_play()
    elif in_rect(kOpenBtn, x, y):
        open_audio_file()
    elif duration_ms > 0 and in_rect(kSeekHit, x, y):
        dragging = True
        position_ms = pos_from_x(x)
        on_paint()

def on_mouse_move(event):
    global position_ms
    if dragging:
        position_ms = pos_from_x(event.x)
        on_paint()

def on_mouse_up(event):
    global dragging
    if dragging:
        dragging = False
        seek_to(pos_from_x(event.x))
        on_paint()

def on_timer():
    global is_playing, position_ms
    if is_playing and not dragging:
        success, buf = mci("status music position", 128)
        if success and buf:
            try:
                position_ms = int(buf)
            except ValueError:
                pass
        
        success, mode = mci("status music mode", 32)
        grace_over = (time.time() * 1000 - play_start_tick) > 400
        if grace_over and "stopped" in mode:
            is_playing = False
            if position_ms >= duration_ms - 500:
                position_ms = 0
                mci("seek music to start")
        on_paint()
    
    root.after(100, on_timer)

def on_closing():
    mci("close music")
    root.destroy()

# --- Main Setup ---
if __name__ == "__main__":
    root = tk.Tk()
    root.title("WMA Player")
    root.geometry(f"{kClientW}x{kClientH}")
    root.resizable(False, False)
    root.protocol("WM_DELETE_WINDOW", on_closing)

    # Apply DWM Immersive Dark Mode for the Titlebar
    root.update()
    try:
        DWMWA_USE_IMMERSIVE_DARK_MODE = 20
        hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
        dark = ctypes.c_int(1) # TRUE
        ctypes.windll.dwmapi.DwmSetWindowAttribute(
            hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, 
            ctypes.byref(dark), ctypes.sizeof(dark)
        )
    except Exception:
        pass # Silently fail on older OS versions where DWM isn't available

    canvas = tk.Canvas(root, width=kClientW, height=kClientH, highlightthickness=0)
    canvas.pack()

    # Bind Events
    canvas.bind("<Button-1>", on_mouse_down)
    canvas.bind("<B1-Motion>", on_mouse_move)
    canvas.bind("<ButtonRelease-1>", on_mouse_up)

    on_paint()     # Initial draw
    on_timer()     # Start loop
    root.mainloop()
