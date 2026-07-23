#!/usr/bin/env python3
import time
import serial
import threading
import xml.etree.ElementTree as ET
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
from svgpathtools import parse_path

# Swapped bed dimensions (Y first, then X layout)
MAX_X = 2400  # Maximum X steps
MAX_Y = 4800  # Maximum Y steps


class LibrecutDevice:
    """Handles serial communication protocol with the Librecut controller."""

    def __init__(self):
        self.ser = None
        self.lock = threading.Lock()

    def is_connected(self):
        return self.ser is not None and self.ser.is_open

    def connect(self, port, baudrate=115200, override_dials=False, speed=100, pressure=200):
        with self.lock:
            self.ser = serial.Serial()
            self.ser.port = port
            self.ser.baudrate = baudrate
            self.ser.timeout = 2
            self.ser.dtr = False
            self.ser.rts = False
            self.ser.open()

            time.sleep(2)  # Wait for AVR bootloader reset cycle
            self.ser.flushInput()

            # Initial connection command sequence
            self._send_raw("connect")
            time.sleep(1.5)

            if override_dials:
                self._send_raw(f"speed {speed}")
                self._send_raw(f"press {pressure}")

    def send_command(self, cmd_str):
        with self.lock:
            return self._send_raw(cmd_str)

    def get_version(self):
        return self.send_command("version")

    def _send_raw(self, cmd_str):
        if not self.is_connected():
            raise ConnectionError("Serial port is not connected.")

        cmd_bytes = (cmd_str.strip() + "\n").encode("ascii")
        self.ser.write(cmd_bytes)

        response = ""
        while True:
            char = self.ser.read(1).decode("ascii", errors="ignore")
            if not char:
                break
            response += char
            if response.endswith(">"):
                break

        return response

    def close(self):
        with self.lock:
            if self.ser and self.ser.is_open:
                try:
                    self.ser.close()
                except Exception:
                    pass
            self.ser = None


def parse_svg_to_commands(svg_filepath, resolution=0.5, spi=393.0, autoscale=False, override_dials=False, speed=100, pressure=200):
    """Parses an SVG file and converts path elements into Librecut motor commands with swapped axes."""
    try:
        tree = ET.parse(svg_filepath)
        root = tree.getroot()
    except Exception as e:
        raise RuntimeError(f"Failed to read SVG file: {e}")

    paths = [elem for elem in root.iter() if elem.tag.endswith("path")]
    raw_points = []

    res = max(0.1, resolution)

    for path_elem in paths:
        d_attr = path_elem.attrib.get("d")
        if not d_attr:
            continue

        try:
            parsed_path = parse_path(d_attr)
        except Exception:
            continue

        subpath_points = []
        for segment in parsed_path:
            seg_len = segment.length()
            num_samples = max(2, int(seg_len / res))
            for i in range(num_samples):
                pt = segment.point(i / (num_samples - 1))
                x_in = pt.real / 96.0
                y_in = pt.imag / 96.0
                subpath_points.append((x_in, y_in))

        if subpath_points:
            raw_points.append(subpath_points)

    if not raw_points:
        return []

    all_x = [pt[0] for sub in raw_points for pt in sub]
    all_y = [pt[1] for sub in raw_points for pt in sub]

    min_x, max_x_val = min(all_x), max(all_x)
    min_y, max_y_val = min(all_y), max(all_y)

    width_in = max_x_val - min_x or 0.01
    height_in = max_y_val - min_y or 0.01

    if autoscale:
        scale_x = MAX_X / (width_in * spi)
        scale_y = MAX_Y / (height_in * spi)
        scale = min(scale_x, scale_y) * 0.95 * spi
    else:
        scale = spi

    commands = []
    if override_dials:
        commands.extend([f"speed {speed}", f"press {pressure}"])

    for subpath in raw_points:
        start_x = int((subpath[0][0] - min_x) * scale)
        start_y = int((subpath[0][1] - min_y) * scale)
        start_x = max(0, min(MAX_X, start_x))
        start_y = max(0, min(MAX_Y, start_y))

        # Swapped X and Y order in command output
        commands.append(f"move {start_y} {start_x}")

        for pt in subpath[1:]:
            cx = int((pt[0] - min_x) * scale)
            cy = int((pt[1] - min_y) * scale)
            cx = max(0, min(MAX_X, cx))
            cy = max(0, min(MAX_Y, cy))

            # Swapped X and Y order in command output
            commands.append(f"draw {cy} {cx}")

    commands.append("move 0 0")
    return commands


class LibrecutCompanionApp(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title("Librecut Companion")
        self.geometry("880x760")
        self.minsize(800, 680)

        self.device = LibrecutDevice()
        self.is_streaming = False
        self.stop_stream_requested = False

        self.curr_x = 0
        self.curr_y = 0

        self._build_ui()
        self._bind_keys()

    def _build_ui(self):
        style = ttk.Style()
        style.theme_use("clam")

        # Connection Frame
        conn_frame = ttk.LabelFrame(self, text=" Connection Settings ", padding=10)
        conn_frame.pack(fill="x", padx=10, pady=5)

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, sticky="w", padx=5)
        self.port_var = tk.StringVar(value="COM13")
        ttk.Entry(conn_frame, textvariable=self.port_var, width=10).grid(row=0, column=1, padx=5)

        ttk.Label(conn_frame, text="Baudrate:").grid(row=0, column=2, sticky="w", padx=5)
        self.baud_var = tk.IntVar(value=115200)
        ttk.Entry(conn_frame, textvariable=self.baud_var, width=10).grid(row=0, column=3, padx=5)

        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.btn_connect.grid(row=0, column=4, padx=10)

        self.btn_version = ttk.Button(conn_frame, text="Check Version", command=self.query_version, state="disabled")
        self.btn_version.grid(row=0, column=5, padx=5)

        self.lbl_status = ttk.Label(conn_frame, text="Status: Disconnected", foreground="red", font=("Segoe UI", 9, "bold"))
        self.lbl_status.grid(row=0, column=6, padx=10)

        # Tabs Notebook
        notebook = ttk.Notebook(self)
        notebook.pack(fill="both", expand=True, padx=10, pady=5)

        # ---------------- TAB 1: Manual Jogging ----------------
        tab_jog = ttk.Frame(notebook, padding=10)
        notebook.add(tab_jog, text=" Manual Controller ")

        jog_grid_frame = ttk.LabelFrame(tab_jog, text=" Directional Jogging (Keyboard WASD Enabled) ", padding=10)
        jog_grid_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")

        ttk.Button(jog_grid_frame, text="▲ Up (+Y) [W]", command=lambda: self.jog_axis('y', 1)).grid(row=0, column=1, pady=5)
        ttk.Button(jog_grid_frame, text="◄ Left (-X) [A]", command=lambda: self.jog_axis('x', -1)).grid(row=1, column=0, padx=5)
        ttk.Button(jog_grid_frame, text="Home (0,0) [H]", command=self.jog_home).grid(row=1, column=1, padx=5, pady=5)
        ttk.Button(jog_grid_frame, text="Right (+X) [D] ►", command=lambda: self.jog_axis('x', 1)).grid(row=1, column=2, padx=5)
        ttk.Button(jog_grid_frame, text="▼ Down (-Y) [S]", command=lambda: self.jog_axis('y', -1)).grid(row=2, column=1, pady=5)

        pos_frame = ttk.LabelFrame(tab_jog, text=" Position & Direct Motion ", padding=10)
        pos_frame.grid(row=0, column=1, padx=10, pady=10, sticky="nsew")

        self.lbl_pos = ttk.Label(pos_frame, text="Current Pos: (0, 0)", font=("Segoe UI", 11, "bold"))
        self.lbl_pos.grid(row=0, column=0, columnspan=4, pady=5, sticky="w")

        ttk.Label(pos_frame, text="Step Size:").grid(row=1, column=0, sticky="w")
        self.step_size_var = tk.IntVar(value=100)
        ttk.Entry(pos_frame, textvariable=self.step_size_var, width=8).grid(row=1, column=1, padx=5, sticky="w")

        ttk.Button(pos_frame, text="- Step", width=7, command=lambda: self.adjust_step(0.5)).grid(row=1, column=2, padx=2)
        ttk.Button(pos_frame, text="+ Step", width=7, command=lambda: self.adjust_step(2.0)).grid(row=1, column=3, padx=2)

        ttk.Separator(pos_frame, orient="horizontal").grid(row=2, column=0, columnspan=4, sticky="ew", pady=10)

        # Move & Draw Direct Inputs
        ttk.Label(pos_frame, text="Move X:").grid(row=3, column=0, sticky="w")
        self.goto_x_var = tk.StringVar(value="0")
        ttk.Entry(pos_frame, textvariable=self.goto_x_var, width=8).grid(row=3, column=1, padx=5)

        ttk.Label(pos_frame, text="Y:").grid(row=3, column=2, sticky="w")
        self.goto_y_var = tk.StringVar(value="0")
        ttk.Entry(pos_frame, textvariable=self.goto_y_var, width=8).grid(row=3, column=3, padx=5)

        btn_box = ttk.Frame(pos_frame)
        btn_box.grid(row=4, column=0, columnspan=4, pady=10)
        ttk.Button(btn_box, text="Move (Pen UP)", command=self.send_move_cmd).pack(side="left", padx=5)
        ttk.Button(btn_box, text="Draw (Pen DOWN)", command=self.send_draw_cmd).pack(side="left", padx=5)

        # ---------------- TAB 2: SVG Streamer ----------------
        tab_stream = ttk.Frame(notebook, padding=10)
        notebook.add(tab_stream, text=" SVG Streamer ")

        file_frame = ttk.Frame(tab_stream)
        file_frame.pack(fill="x", pady=5)

        ttk.Label(file_frame, text="SVG File:").pack(side="left")
        self.svg_path_var = tk.StringVar()
        ttk.Entry(file_frame, textvariable=self.svg_path_var).pack(side="left", fill="x", expand=True, padx=5)
        ttk.Button(file_frame, text="Browse...", command=self.browse_svg).pack(side="left")

        opts_frame = ttk.LabelFrame(tab_stream, text=" Cutting Parameters ", padding=10)
        opts_frame.pack(fill="x", pady=10)

        self.override_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(opts_frame, text="Override Dials", variable=self.override_var).grid(row=0, column=0, sticky="w", padx=5, pady=5)

        self.autoscale_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(opts_frame, text="Autoscale to Bed", variable=self.autoscale_var).grid(row=0, column=2, sticky="w", padx=5, pady=5)

        ttk.Label(opts_frame, text="Speed:").grid(row=1, column=0, sticky="w", padx=5)
        self.speed_var = tk.IntVar(value=100)
        ttk.Entry(opts_frame, textvariable=self.speed_var, width=8).grid(row=1, column=1, sticky="w", padx=5)

        ttk.Label(opts_frame, text="Pressure:").grid(row=1, column=2, sticky="w", padx=5)
        self.press_var = tk.IntVar(value=200)
        ttk.Entry(opts_frame, textvariable=self.press_var, width=8).grid(row=1, column=3, sticky="w", padx=5)

        ttk.Label(opts_frame, text="Resolution:").grid(row=2, column=0, sticky="w", padx=5, pady=5)
        self.res_var = tk.DoubleVar(value=0.5)
        ttk.Entry(opts_frame, textvariable=self.res_var, width=8).grid(row=2, column=1, sticky="w", padx=5)

        ttk.Label(opts_frame, text="Steps / Inch:").grid(row=2, column=2, sticky="w", padx=5, pady=5)
        self.spi_var = tk.DoubleVar(value=393.0)
        ttk.Entry(opts_frame, textvariable=self.spi_var, width=8).grid(row=2, column=3, sticky="w", padx=5)

        ctrl_frame = ttk.Frame(tab_stream)
        ctrl_frame.pack(fill="x", pady=5)

        self.btn_start_stream = ttk.Button(ctrl_frame, text="Start Streaming", command=self.start_stream)
        self.btn_start_stream.pack(side="left", padx=5)

        self.btn_stop_stream = ttk.Button(ctrl_frame, text="Stop", command=self.stop_stream, state="disabled")
        self.btn_stop_stream.pack(side="left", padx=5)

        self.progress_bar = ttk.Progressbar(ctrl_frame, orient="horizontal", mode="determinate")
        self.progress_bar.pack(side="left", fill="x", expand=True, padx=10)

        # ---------------- TAB 3: Advanced Vector (Curve & HPGL) ----------------
        tab_adv = ttk.Frame(notebook, padding=10)
        notebook.add(tab_adv, text=" Bezier & HPGL ")

        # Bezier Curve Frame
        curve_frame = ttk.LabelFrame(tab_adv, text=" Cubic Bezier Curve ('curve P0x P0y P1x P1y P2x P2y P3x P3y') ", padding=10)
        curve_frame.pack(fill="x", pady=5)

        self.curve_pts = [tk.StringVar(value="0") for _ in range(8)]
        pt_labels = ["P0 X:", "Y:", "P1 X:", "Y:", "P2 X:", "Y:", "P3 X:", "Y:"]
        
        for idx, lbl in enumerate(pt_labels):
            row = idx // 4
            col = (idx % 4) * 2
            ttk.Label(curve_frame, text=lbl).grid(row=row, column=col, padx=3, pady=3, sticky="e")
            ttk.Entry(curve_frame, textvariable=self.curve_pts[idx], width=6).grid(row=row, column=col+1, padx=3, pady=3)

        ttk.Button(curve_frame, text="Send Bezier Curve", command=self.send_curve_cmd).grid(row=2, column=0, columnspan=8, pady=8)

        # HPGL Frame
        hpgl_frame = ttk.LabelFrame(tab_adv, text=" HPGL Command Spooler ('hpgl <command string>') ", padding=10)
        hpgl_frame.pack(fill="both", expand=True, pady=5)

        self.hpgl_var = tk.StringVar(value="IN;SP1;PA0,0;PD100,100,100,0,0,0;PU;")
        ttk.Entry(hpgl_frame, textvariable=self.hpgl_var).pack(fill="x", padx=5, pady=5)
        ttk.Button(hpgl_frame, text="Send HPGL String", command=self.send_hpgl_cmd).pack(pady=5)

        # ---------------- TAB 4: Board Utilities & Hardware ----------------
        tab_hw = ttk.Frame(notebook, padding=10)
        notebook.add(tab_hw, text=" Hardware & Flash ")

        # Keypad LEDs Frame
        led_frame = ttk.LabelFrame(tab_hw, text=" Keypad LEDs Controls ('leds', 'led') ", padding=10)
        led_frame.pack(fill="x", pady=5)

        ttk.Button(led_frame, text="Enable All LEDs", command=lambda: self.send_raw_cmd("leds 1")).grid(row=0, column=0, padx=5, pady=5)
        ttk.Button(led_frame, text="Disable All LEDs", command=lambda: self.send_raw_cmd("leds 0")).grid(row=0, column=1, padx=5, pady=5)

        ttk.Label(led_frame, text="Single LED ID (e.g. A1, B3):").grid(row=0, column=2, padx=5)
        self.single_led_var = tk.StringVar(value="A1")
        ttk.Entry(led_frame, textvariable=self.single_led_var, width=6).grid(row=0, column=3, padx=5)
        ttk.Button(led_frame, text="Toggle Single LED", command=self.send_single_led_cmd).grid(row=0, column=4, padx=5)

        # Flash Storage Operations Frame
        flash_frame = ttk.LabelFrame(tab_hw, text=" On-Board Flash Operations ('flash', 'flashwrite') ", padding=10)
        flash_frame.pack(fill="x", pady=5)

        ttk.Button(flash_frame, text="Run Flash Test", command=lambda: self.send_raw_cmd("flash")).grid(row=0, column=0, padx=5, pady=5)

        ttk.Label(flash_frame, text="Page (0-2047):").grid(row=0, column=1, padx=5)
        self.flash_page_var = tk.StringVar(value="0")
        ttk.Entry(flash_frame, textvariable=self.flash_page_var, width=6).grid(row=0, column=2, padx=5)

        ttk.Label(flash_frame, text="Bytes (Space separated):").grid(row=0, column=3, padx=5)
        self.flash_bytes_var = tk.StringVar(value="255 128 64 32")
        ttk.Entry(flash_frame, textvariable=self.flash_bytes_var, width=25).grid(row=0, column=4, padx=5)

        ttk.Button(flash_frame, text="Write Flash Page", command=self.send_flashwrite_cmd).grid(row=0, column=5, padx=5)

        # System Diagnostics Frame
        sys_frame = ttk.LabelFrame(tab_hw, text=" System Diagnostics & Hardware Reset ", padding=10)
        sys_frame.pack(fill="x", pady=5)

        ttk.Button(sys_frame, text="Trigger Controller Reset ('reset')", command=self.send_reset_cmd).pack(side="left", padx=10, pady=5)

        # Console Output Log
        log_frame = ttk.LabelFrame(self, text=" Controller Console Output ", padding=5)
        log_frame.pack(fill="both", expand=True, padx=10, pady=5)

        self.log_text = scrolledtext.ScrolledText(log_frame, height=10, font=("Consolas", 9), state="disabled", background="#1e1e1e", foreground="#00ff00")
        self.log_text.pack(fill="both", expand=True)

    def _bind_keys(self):
        self.bind("<w>", lambda e: self.jog_axis('y', 1))
        self.bind("<s>", lambda e: self.jog_axis('y', -1))
        self.bind("<a>", lambda e: self.jog_axis('x', -1))
        self.bind("<d>", lambda e: self.jog_axis('x', 1))
        self.bind("<h>", lambda e: self.jog_home())

    def log(self, message):
        self.log_text.config(state="normal")
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state="disabled")

    def send_raw_cmd(self, cmd_str):
        if not self.device.is_connected():
            messagebox.showwarning("Not Connected", "Please connect to the Librecut controller first.")
            return

        def worker():
            res = self.device.send_command(cmd_str)
            self.log(f"> {cmd_str} | Response: {res.strip()}")

        threading.Thread(target=worker, daemon=True).start()

    def toggle_connection(self):
        if self.device.is_connected():
            self.device.close()
            self.lbl_status.config(text="Status: Disconnected", foreground="red")
            self.btn_connect.config(text="Connect")
            self.btn_version.config(state="disabled")
            self.log("[System] Disconnected from serial port.")
        else:
            port = self.port_var.get().strip()
            baud = self.baud_var.get()
            try:
                self.device.connect(
                    port=port,
                    baudrate=baud,
                    override_dials=self.override_var.get(),
                    speed=self.speed_var.get(),
                    pressure=self.press_var.get(),
                )
                self.lbl_status.config(text=f"Status: Connected ({port})", foreground="green")
                self.btn_connect.config(text="Disconnect")
                self.btn_version.config(state="normal")
                self.log(f"[System] Connected to {port} at {baud} baud.")

                self.query_version()

            except Exception as e:
                messagebox.showerror("Connection Error", f"Could not connect to {port}:\n{e}")
                self.log(f"[Error] Connection failed: {e}")

    def query_version(self):
        if not self.device.is_connected():
            return

        def worker():
            res = self.device.get_version()
            clean_res = res.strip().rstrip('>')
            self.log(f"[Device] Firmware Version: {clean_res}")

        threading.Thread(target=worker, daemon=True).start()

    # --- Jogging & Motion Handlers ---
    def jog_axis(self, axis, direction):
        if not self.device.is_connected():
            return

        step = self.step_size_var.get()
        new_x = self.curr_x
        new_y = self.curr_y

        if axis == 'y':
            new_y = max(0, min(MAX_Y, self.curr_y + (direction * step)))
        elif axis == 'x':
            new_x = max(0, min(MAX_X, self.curr_x + (direction * step)))

        self._execute_move(new_x, new_y)

    def jog_home(self):
        if not self.device.is_connected():
            return
        self._execute_move(0, 0)

    def send_move_cmd(self):
        try:
            gx = max(0, min(MAX_X, int(self.goto_x_var.get().strip())))
            gy = max(0, min(MAX_Y, int(self.goto_y_var.get().strip())))
            self._execute_move(gx, gy, action_cmd="move")
        except ValueError:
            messagebox.showwarning("Invalid Input", "Please enter valid integers for X and Y coordinates.")

    def send_draw_cmd(self):
        try:
            gx = max(0, min(MAX_X, int(self.goto_x_var.get().strip())))
            gy = max(0, min(MAX_Y, int(self.goto_y_var.get().strip())))
            self._execute_move(gx, gy, action_cmd="draw")
        except ValueError:
            messagebox.showwarning("Invalid Input", "Please enter valid integers for X and Y coordinates.")

    def adjust_step(self, factor):
        new_val = max(10, int(self.step_size_var.get() * factor))
        self.step_size_var.set(new_val)

    def _execute_move(self, target_x, target_y, action_cmd="move"):
        def worker():
            # Swapped X and Y order in the command string sent to firmware
            cmd = f"{action_cmd} {target_y} {target_x}"
            res = self.device.send_command(cmd)
            self.curr_x, self.curr_y = target_x, target_y
            self.lbl_pos.config(text=f"Current Pos: ({self.curr_x}, {self.curr_y})")
            self.log(f"> {cmd} | Response: {res.strip()}")

        threading.Thread(target=worker, daemon=True).start()

    # --- Curve & HPGL Handlers ---
    def send_curve_cmd(self):
        try:
            pts = [int(v.get().strip()) for v in self.curve_pts]
            cmd = f"curve {' '.join(map(str, pts))}"
            self.send_raw_cmd(cmd)
        except ValueError:
            messagebox.showwarning("Invalid Curve Data", "All 8 Bezier control points must be integers.")

    def send_hpgl_cmd(self):
        hstr = self.hpgl_var.get().strip()
        if hstr:
            self.send_raw_cmd(f"hpgl {hstr}")

    # --- Hardware Utility Handlers ---
    def send_single_led_cmd(self):
        led_id = self.single_led_var.get().strip()
        if led_id:
            self.send_raw_cmd(f"led {led_id}")

    def send_flashwrite_cmd(self):
        page = self.flash_page_var.get().strip()
        bytes_str = self.flash_bytes_var.get().strip()
        if page and bytes_str:
            self.send_raw_cmd(f"flashwrite {page} {bytes_str}")

    def send_reset_cmd(self):
        if messagebox.askyesno("Confirm Hardware Reset", "This will trigger the watchdog timer reset on the controller. Continue?"):
            self.send_raw_cmd("reset")

    # --- Streaming Handlers ---
    def browse_svg(self):
        filename = filedialog.askopenfilename(filetypes=[("SVG Files", "*.svg"), ("All Files", "*.*")])
        if filename:
            self.svg_path_var.set(filename)

    def start_stream(self):
        if not self.device.is_connected():
            messagebox.showwarning("Not Connected", "Please connect to the Librecut controller first.")
            return

        filepath = self.svg_path_var.get().strip()
        if not filepath:
            messagebox.showwarning("No File", "Please select an SVG file to stream.")
            return

        try:
            cmds = parse_svg_to_commands(
                svg_filepath=filepath,
                resolution=self.res_var.get(),
                spi=self.spi_var.get(),
                autoscale=self.autoscale_var.get(),
                override_dials=self.override_var.get(),
                speed=self.speed_var.get(),
                pressure=self.press_var.get(),
            )
        except Exception as e:
            messagebox.showerror("SVG Error", f"Error parsing SVG:\n{e}")
            return

        if not cmds:
            messagebox.showinfo("No Paths", "No valid vector paths found in the selected SVG.")
            return

        self.is_streaming = True
        self.stop_stream_requested = False
        self.btn_start_stream.config(state="disabled")
        self.btn_stop_stream.config(state="normal")
        self.progress_bar["value"] = 0

        threading.Thread(target=self._stream_worker, args=(cmds,), daemon=True).start()

    def stop_stream(self):
        if self.is_streaming:
            self.stop_stream_requested = True
            self.log("[System] Cancel requested. Stopping stream after current command...")

    def _stream_worker(self, commands):
        total = len(commands)
        self.log(f"[Stream] Starting file stream ({total} commands)...")

        for idx, cmd in enumerate(commands, 1):
            if self.stop_stream_requested:
                self.log("[Stream] Streaming cancelled by user.")
                break

            self.device.send_command(f"progress {idx} {total}")
            res = self.device.send_command(cmd)

            pct = (idx / total) * 100
            self.progress_bar["value"] = pct
            self.log(f"[{idx}/{total}] > {cmd} | Response: {res.strip()}")

        if not self.stop_stream_requested:
            self.device.send_command("done")
            self.log("[Stream] Finished streaming successfully!")

        self.is_streaming = False
        self.btn_start_stream.config(state="normal")
        self.btn_stop_stream.config(state="disabled")


if __name__ == "__main__":
    app = LibrecutCompanionApp()
    app.mainloop()