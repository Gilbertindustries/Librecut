#!/usr/bin/env python3
import time
import serial

PORT = "COM13"  # Adjust to your COM port or /dev/ttyUSB0
BAUDRATE = 115200


def send_command(ser, cmd_str):
    """Sends a string command to the Librecut controller and waits for response prompt '>'."""
    cmd_bytes = (cmd_str.strip() + "\n").encode("ascii")
    ser.write(cmd_bytes)

    response = ""
    while True:
        char = ser.read(1).decode("ascii", errors="ignore")
        if not char:
            break
        response += char
        if response.endswith(">"):
            break
    return response


def main():
    print("==========================================")
    print("      Librecut Interactive Controller     ")
    print("==========================================")

    port_input = input(f"Enter serial port [{PORT}]: ").strip()
    target_port = port_input if port_input else PORT

    try:
        ser = serial.Serial(target_port, BAUDRATE, timeout=2)
        print(f"\nConnected to {target_port} at {BAUDRATE} baud.")
        time.sleep(2)  # Wait for serial connection to stabilize
        ser.flushInput()
    except Exception as e:
        print(f"\n[Error] Could not open port: {e}")
        return

    # Keep track of current positions and step distance
    curr_x, curr_y = 0, 0
    step_size = 100  # Default movement step in motor units

    print("\nControls:")
    print("  [W] Move UP (+Y)")
    print("  [S] Move DOWN (-Y)")
    print("  [A] Move LEFT (-X)")
    print("  [D] Move RIGHT (+X)")
    print("  [+] Increase step size")
    print("  [-] Decrease step size")
    print("  [H] Home to (0,0)")
    print("  [G] Go to specific (X, Y) coordinate")
    print("  [Q] Quit")
    print("------------------------------------------")

    try:
        while True:
            cmd = (
                input(
                    f"Pos: ({curr_x}, {curr_y}) | Step: {step_size} > Choice: "
                )
                .strip()
                .lower()
            )

            if not cmd:
                continue

            if cmd == "q":
                print("Exiting controller...")
                break

            elif cmd == "w":
                curr_y += step_size
                send_command(ser, f"move {curr_x} {curr_y}")
            elif cmd == "s":
                curr_y = max(0, curr_y - step_size)
                send_command(ser, f"move {curr_x} {curr_y}")
            elif cmd == "a":
                curr_x = max(0, curr_x - step_size)
                send_command(ser, f"move {curr_x} {curr_y}")
            elif cmd == "d":
                curr_x += step_size
                send_command(ser, f"move {curr_x} {curr_y}")

            elif cmd == "+":
                step_size = int(step_size * 2)
                print(f"Step size set to: {step_size}")
            elif cmd == "-":
                step_size = max(10, int(step_size / 2))
                print(f"Step size set to: {step_size}")

            elif cmd == "h":
                curr_x, curr_y = 0, 0
                send_command(ser, "move 0 0")
                print("Moved back to Origin (0,0).")

            elif cmd == "g":
                try:
                    coords = input(
                        "Enter target X Y coordinates (e.g. 500 300): "
                    ).split()
                    new_x, new_y = int(coords[0]), int(coords[1])
                    curr_x, curr_y = new_x, new_y
                    send_command(ser, f"move {curr_x} {curr_y}")
                except (ValueError, IndexError):
                    print("Invalid coordinates format.")
            else:
                print("Unknown command.")

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        ser.close()
        print("Serial port closed.")


if __name__ == "__main__":
    main()