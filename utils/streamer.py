import time
import serial
from svgpathtools import svg2paths

# ---------------------------------------------------------------------------
# Machine Configuration Boundaries (from stepper.c)
# ---------------------------------------------------------------------------
MAX_X = 4800  # Maximum X steps
MAX_Y = 2400  # Maximum Y steps

print("Gilbert Industries Librecutter command streamer")
class FreecutStreamer:

    def __init__(self, port, baudrate=115200, speed=100, pressure=200):
        self.port = port
        self.baudrate = baudrate
        self.speed = speed
        self.pressure = pressure
        self.ser = None

    def connect(self):
        """Establishes connection with the Freecut controller."""
        print(f"Connecting to Freecut on {self.port} at {self.baudrate} baud...")
        self.ser = serial.Serial(self.port, self.baudrate, timeout=2)
        time.sleep(2)  # Wait for AVR bootloader reset cycle
        self.ser.flushInput()

        # Set default speed and pressure parameters
        self.send_command(f"speed {self.speed}")
        self.send_command(f"press {self.pressure}")

    def send_command(self, cmd_str):
        """Sends a single line command and waits for the CLI prompt ('>')."""
        if not self.ser or not self.ser.is_open:
            raise ConnectionError("Serial port is not open.")

        cmd_bytes = (cmd_str.strip() + "\n").encode("ascii")
        self.ser.write(cmd_bytes)

        # Read back response until the CLI prompt '>' is received
        response = ""
        while True:
            char = self.ser.read(1).decode("ascii", errors="ignore")
            if not char:
                break  # Read timeout
            response += char
            if response.endswith(">"):
                break

        return response

    def convert_svg_to_commands(self, svg_filepath):
        """Extracts paths from an SVG file and scales them to fit within
        MAX_X (4800) and MAX_Y (2400) machine limits.
        """
        paths, _ = svg2paths(svg_filepath)
        raw_points = []

        # 1. Sample points along all SVG paths
        for path in paths:
            subpath_points = []
            for segment in path:
                # Sample start point and end point of segment
                num_samples = max(2, int(segment.length() / 5))
                for i in range(num_samples):
                    pt = segment.point(i / (num_samples - 1))
                    subpath_points.append((pt.real, pt.imag))
            if subpath_points:
                raw_points.append(subpath_points)

        if not raw_points:
            print("No valid paths found in SVG.")
            return []

        # 2. Compute bounding box of the input vector data
        all_x = [pt[0] for sub in raw_points for pt in sub]
        all_y = [pt[1] for sub in raw_points for pt in sub]

        min_x, max_x = min(all_x), max(all_x)
        min_y, max_y = min(all_y), max(all_y)

        width = max_x - min_x or 1.0
        height = max_y - min_y or 1.0

        # 3. Calculate uniform scaling factor to fit inside 4800 x 2400
        scale_x = MAX_X / width
        scale_y = MAX_Y / height
        scale = min(scale_x, scale_y) * 0.95  # 95% scale for safety margin

        # 4. Generate native Freecut CLI commands
        commands = [f"speed {self.speed}", f"press {self.pressure}"]

        for subpath in raw_points:
            # Move to start of subpath (Pen UP)
            start_x = int((subpath[0][0] - min_x) * scale)
            start_y = int((subpath[0][1] - min_y) * scale)
            commands.append(f"move {start_x} {start_y}")

            # Draw along subpath points (Pen DOWN)
            for pt in subpath[1:]:
                cx = int((pt[0] - min_x) * scale)
                cy = int((pt[1] - min_y) * scale)

                # Clamp strictly to machine limits
                cx = max(0, min(MAX_X, cx))
                cy = max(0, min(MAX_Y, cy))
                commands.append(f"draw {cx} {cy}")

        # Finish job and return to home
        commands.append("move 0 0")
        return commands

    def stream_file(self, svg_filepath):
        commands = self.convert_svg_to_commands(svg_filepath)
        total_cmds = len(commands)

        print(f"Generated {total_cmds} commands. Starting stream to cutter...")

        for idx, cmd in enumerate(commands, 1):
            # 1. Update the LCD screen progress display [X/TOTAL]
            self.send_command(f"progress {idx} {total_cmds}")

            # 2. Execute the actual motion command
            print(f"[{idx}/{total_cmds}] Executing: {cmd}")
            self.send_command(cmd)

        # 3. Job completed: clear screen and show Done!
        self.send_command("done")
        print("Cut job completed successfully!")

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()


# ---------------------------------------------------------------------------
# Main Execution Entry Point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    COM_PORT = "COM13"
    SVG_FILE = "test3.svg"

    streamer = FreecutStreamer(port=COM_PORT, speed=100, pressure=200)

    try:
        streamer.connect()
        streamer.stream_file(SVG_FILE)
    except Exception as e:
        print(f"Error during streaming: {e}")
    finally:
        streamer.close()