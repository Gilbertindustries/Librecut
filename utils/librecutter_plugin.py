#!/usr/bin/env python3
import time
import serial
import inkex
from svgpathtools import parse_path

MAX_X = 4800  # Maximum X steps
MAX_Y = 2400  # Maximum Y steps


class FreecutStreamer:

    def __init__(
        self,
        port,
        baudrate=115200,
        speed=100,
        pressure=200,
        resolution=2,
        autoscale=False,
        steps_per_inch=393.0,
    ):
        self.port = port
        self.baudrate = baudrate
        self.speed = speed
        self.pressure = pressure
        self.resolution = max(0.1, resolution)
        self.autoscale = autoscale
        self.spi = steps_per_inch
        self.ser = None

    def connect(self):
        self.ser = serial.Serial(self.port, self.baudrate, timeout=2)
        time.sleep(2)  # Wait for AVR bootloader reset cycle
        self.ser.flushInput()

        self.send_command(f"speed {self.speed}")
        self.send_command(f"press {self.pressure}")

    def send_command(self, cmd_str):
        if not self.ser or not self.ser.is_open:
            raise ConnectionError("Serial port is not open.")

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

    def convert_svg_to_commands(self, svg_obj):
        raw_points = []

        # Convert Inkscape's document unit to inches conversion factor
        doc_unit = svg_obj.unit
        units_per_inch = inkex.units.convert_unit("1in", doc_unit)

        # 1. Iterate through all SVG path elements and apply transform matrices
        for node in svg_obj.xpath("//svg:path"):
            abs_path = node.path.to_absolute().transform(
                node.composed_transform()
            )
            parsed_path = parse_path(str(abs_path))

            subpath_points = []
            for segment in parsed_path:
                # Calculate sample density based on user resolution setting
                num_samples = max(2, int(segment.length() / self.resolution))
                for i in range(num_samples):
                    pt = segment.point(i / (num_samples - 1))
                    x_in = pt.real / units_per_inch
                    y_in = pt.imag / units_per_inch
                    subpath_points.append((x_in, y_in))

            if subpath_points:
                raw_points.append(subpath_points)

        if not raw_points:
            return []

        # 2. Compute bounding box in real inches
        all_x = [pt[0] for sub in raw_points for pt in sub]
        all_y = [pt[1] for sub in raw_points for pt in sub]

        min_x, max_x = min(all_x), max(all_x)
        min_y, max_y = min(all_y), max(all_y)

        width_in = max_x - min_x or 0.01
        height_in = max_y - min_y or 0.01

        # 3. Calculate step scaling factor
        if self.autoscale:
            scale_x = MAX_X / (width_in * self.spi)
            scale_y = MAX_Y / (height_in * self.spi)
            scale = min(scale_x, scale_y) * 0.95 * self.spi
        else:
            scale = self.spi

        # 4. Generate native CLI commands
        commands = [f"speed {self.speed}", f"press {self.pressure}"]

        for subpath in raw_points:
            # Move to start (Pen UP)
            start_x = int((subpath[0][0] - min_x) * scale)
            start_y = int((subpath[0][1] - min_y) * scale)

            start_x = max(0, min(MAX_X, start_x))
            start_y = max(0, min(MAX_Y, start_y))
            commands.append(f"move {start_x} {start_y}")

            # Draw along path (Pen DOWN)
            for pt in subpath[1:]:
                cx = int((pt[0] - min_x) * scale)
                cy = int((pt[1] - min_y) * scale)

                cx = max(0, min(MAX_X, cx))
                cy = max(0, min(MAX_Y, cy))
                commands.append(f"draw {cx} {cy}")

        commands.append("move 0 0")
        return commands

    def stream_file(self, svg_obj):
        commands = self.convert_svg_to_commands(svg_obj)
        total_cmds = len(commands)

        if total_cmds == 0:
            inkex.errormsg("No valid paths found in the current drawing.")
            return

        for idx, cmd in enumerate(commands, 1):
            self.send_command(f"progress {idx} {total_cmds}")
            self.send_command(cmd)

        self.send_command("done")

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()


class FreecutInkscapeExtension(inkex.EffectExtension):

    def add_arguments(self, pars):
        pars.add_argument("--port", type=str, default="COM13")
        pars.add_argument("--baudrate", type=int, default=115200)
        pars.add_argument("--speed", type=int, default=100)
        pars.add_argument("--pressure", type=int, default=200)
        pars.add_argument("--resolution", type=float, default=0.5)
        pars.add_argument("--autoscale", type=inkex.Boolean, default=False)
        pars.add_argument("--spi", type=float, default=393.0)

    def effect(self):
        streamer = FreecutStreamer(
            port=self.options.port,
            baudrate=self.options.baudrate,
            speed=self.options.speed,
            pressure=self.options.pressure,
            resolution=self.options.resolution,
            autoscale=self.options.autoscale,
            steps_per_inch=self.options.spi,
        )

        try:
            streamer.connect()
            streamer.stream_file(self.svg)
        except Exception as e:
            inkex.errormsg(f"Error streaming to Librecut: {e}")
        finally:
            streamer.close()


if __name__ == "__main__":
    FreecutInkscapeExtension().run()