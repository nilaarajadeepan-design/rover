"""
=====================================================================
ROVER REMOTE CONTROL - Python script for your Dell laptop
=====================================================================
"""

import socket
import select
import time
from datetime import datetime
import tkinter as tk
from tkinter import messagebox
from pynput import keyboard

ROVER_IP = "192.168.1.104"
ROVER_PORT = 8888
SEND_RATE_HZ = 10
LOG_FILE_NAME = "rover_log.txt"
SPEED_NAMES = {'1': 'SLOW', '2': 'MEDIUM', '3': 'FAST'}
current_speed_label = "MEDIUM"
sock = None

incoming_buffer = ""
current_distance_mm = 0.0
last_logged_distance_mm = 0.0

_popup_root = tk.Tk()
_popup_root.withdraw()


def log_message(text):
    timestamp = datetime.now().strftime("%H:%M:%S")
    full_line = f"[{timestamp}] {text}"
    print(full_line)
    with open(LOG_FILE_NAME, "a") as log_file:
        log_file.write(full_line + "\n")


def show_error_popup(title, message):
    log_message(f"ERROR - {title}: {message}")
    messagebox.showerror(title, message)


def check_for_distance_updates():
    global incoming_buffer, current_distance_mm, last_logged_distance_mm

    readable, _, _ = select.select([sock], [], [], 0)
    if sock not in readable:
        return True

    try:
        chunk = sock.recv(4096)
    except Exception as e:
        show_error_popup("Lost connection to rover", f"Could not read data.\n\nError detail: {e}")
        return False

    if not chunk:
        show_error_popup("Lost connection to rover", "The rover closed the connection unexpectedly.")
        return False

    incoming_buffer += chunk.decode(errors="ignore")

    while "\n" in incoming_buffer:
        line, incoming_buffer = incoming_buffer.split("\n", 1)
        line = line.strip()
        if line.startswith("DIST:"):
            try:
                current_distance_mm = float(line[5:])
            except ValueError:
                pass
            if abs(current_distance_mm - last_logged_distance_mm) >= 10:
                distance_cm = current_distance_mm / 10.0
                log_message(f"Distance traveled: {distance_cm:.1f} cm")
                last_logged_distance_mm = current_distance_mm

    return True


pressed_keys = set()
should_quit = False


def on_key_press(key):
    global should_quit
    if key == keyboard.Key.esc:
        should_quit = True
        return False
    if key in (keyboard.Key.up, keyboard.Key.down, keyboard.Key.left, keyboard.Key.right):
        pressed_keys.add(key)
        log_message(f"Key pressed: {key_name(key)}")
        return
    if hasattr(key, "char") and key.char in SPEED_NAMES:
        set_speed(key.char)


def set_speed(number_key):
    global current_speed_label
    current_speed_label = SPEED_NAMES[number_key]
    log_message(f"Speed set to: {current_speed_label}")
    try:
        sock.sendall(number_key.encode())
    except Exception as e:
        show_error_popup("Lost connection to rover", f"Could not send speed command.\n\nError detail: {e}")


def on_key_release(key):
    if key in pressed_keys:
        pressed_keys.discard(key)
        log_message(f"Key released: {key_name(key)}")


def key_name(key):
    names = {
        keyboard.Key.up: "UP", keyboard.Key.down: "DOWN",
        keyboard.Key.left: "LEFT", keyboard.Key.right: "RIGHT",
    }
    return names.get(key, str(key))


def decide_command():
    if keyboard.Key.up in pressed_keys:
        return 'F'
    elif keyboard.Key.down in pressed_keys:
        return 'B'
    elif keyboard.Key.left in pressed_keys:
        return 'L'
    elif keyboard.Key.right in pressed_keys:
        return 'R'
    else:
        return 'S'


def main():
    global sock
    log_message("=" * 50)
    log_message("ROVER REMOTE CONTROL - session started")
    log_message("=" * 50)
    log_message(f"Connecting to rover at {ROVER_IP}:{ROVER_PORT} ...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((ROVER_IP, ROVER_PORT))
    except Exception as e:
        show_error_popup(
            "Could not connect to rover",
            f"Things to check:\n1. Is the rover powered on?\n2. Correct IP address?\n3. Same WiFi?\n\nError detail: {e}"
        )
        return

    log_message("Connected to rover!")
    print("\nControls:")
    print("  Up arrow    = Forward")
    print("  Down arrow  = Backward")
    print("  Left arrow  = Turn left")
    print("  Right arrow = Turn right")
    print("  1 / 2 / 3   = Speed: Slow / Medium / Fast")
    print("  ESC         = Quit\n")
    print("Distance traveled will be logged automatically as you drive.")
    print("Click this window to make sure it's active, then drive!")

    listener = keyboard.Listener(on_press=on_key_press, on_release=on_key_release)
    listener.start()

    last_command_sent = None

    try:
        while not should_quit:
            command = decide_command()
            if command != last_command_sent:
                labels = {'F': 'FORWARD', 'B': 'BACKWARD', 'L': 'TURN LEFT', 'R': 'TURN RIGHT', 'S': 'STOP'}
                log_message(f"Rover action: {labels[command]} (speed: {current_speed_label})")
                last_command_sent = command

            try:
                sock.sendall(command.encode())
            except Exception as e:
                show_error_popup("Lost connection to rover", f"Connection dropped.\n\nError detail: {e}")
                break

            time.sleep(1.0 / SEND_RATE_HZ)

            if not check_for_distance_updates():
                break

    except KeyboardInterrupt:
        pass

    finally:
        try:
            sock.sendall('S'.encode())
        except Exception:
            pass
        sock.close()
        listener.stop()
        log_message(f"Final distance traveled: {current_distance_mm / 10.0:.1f} cm")
        log_message("Disconnected. Rover stopped. Session ended.")
        _popup_root.destroy()


if __name__ == "__main__":
    main()
