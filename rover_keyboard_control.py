"""
=====================================================================
ROVER REMOTE CONTROL - Python script for your Dell laptop
=====================================================================

WHAT THIS SCRIPT DOES:
1. Connects to your rover over WiFi (using the IP address the ESP32
   printed to the Serial Monitor when it connected to "Bluesky")
2. Watches your keyboard for arrow key presses
3. While an arrow key is held down, sends the matching command to
   the rover ("F" for forward, "B" for backward, "L" for left,
   "R" for right)
4. When no arrow key is held, sends "S" (stop)
5. LOGS every key press and what the rover did - both printed to the
   screen AND saved to a text file (rover_log.txt) with timestamps,
   so you have a written record for your science fair notebook
6. Pops up a real error window (not just text) if the rover can't be
   reached or the connection drops, so you never miss an error
7. Press 1, 2, or 3 anytime to set the rover's speed to SLOW, MEDIUM,
   or FAST - works even while you're driving

This uses two extra Python libraries you need to install first.
Open a terminal (Command Prompt) on your Dell laptop and run:

    pip install pynput

(You do NOT need to install "socket", "datetime", or "tkinter" -
those all come built into Python already.)

HOW TO RUN THIS SCRIPT:
1. Make sure your rover is powered on and connected to WiFi (check
   the Serial Monitor for the "ROVER IP ADDRESS" message)
2. Fill in that IP address below where it says ROVER_IP
3. Make sure your laptop is connected to the SAME WiFi network
   ("Bluesky") as the rover
4. Run this file:  python rover_keyboard_control.py
5. Click on this program's terminal window so it has "focus"
   (arrow keys only work if this window is the active one)
6. Press and hold arrow keys to drive! Press ESC to quit.
=====================================================================
"""

import socket
import time
from datetime import datetime
import tkinter as tk
from tkinter import messagebox
from pynput import keyboard

# ============================= SETTINGS =============================
# Fill in the IP address your ESP32 printed to the Serial Monitor.
ROVER_IP = "192.168.1.104"

# This must match the SERVER_PORT number in the ESP32 code (8888).
ROVER_PORT = 8888

# How many times per second we send a command while a key is held.
SEND_RATE_HZ = 10

# Every log line also gets saved to this text file.
LOG_FILE_NAME = "rover_log.txt"

# Maps the number key you press to a human-readable speed name.
SPEED_NAMES = {'1': 'SLOW', '2': 'MEDIUM', '3': 'FAST'}

# Keeps track of the speed currently selected.
current_speed_label = "MEDIUM"

# This will hold our network connection once main() creates it.
sock = None

# Tracks which arrow keys are currently held down.
pressed_keys = set()

# Becomes True when we want the whole program to stop.
should_quit = False

# tkinter needs one hidden "root" window before popups will work.
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


def on_key_press(key):
    global should_quit

    if key == keyboard.Key.esc:
        should_quit = True
        return False

    if key in (keyboard.Key.up, keyboard.Key.down,
               keyboard.Key.left, keyboard.Key.right):
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
        show_error_popup(
            "Lost connection to rover",
            f"Could not send speed command.\n\nError detail: {e}"
        )


def on_key_release(key):
    if key in pressed_keys:
        pressed_keys.discard(key)
        log_message(f"Key released: {key_name(key)}")


def key_name(key):
    names = {
        keyboard.Key.up: "UP",
        keyboard.Key.down: "DOWN",
        keyboard.Key.left: "LEFT",
        keyboard.Key.right: "RIGHT",
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
            "Things to check:\n"
            "1. Is the rover powered on?\n"
            "2. Did you type the correct IP address in the script?\n"
            "3. Is your laptop connected to the 'Bluesky' WiFi?\n\n"
            f"Error detail: {e}"
        )
        return

    log_message("Connected to rover!")
    print()
    print("Controls:")
    print("  Up arrow    = Forward")
    print("  Down arrow  = Backward")
    print("  Left arrow  = Turn left")
    print("  Right arrow = Turn right")
    print("  1 / 2 / 3   = Speed: Slow / Medium / Fast")
    print("  ESC         = Quit")
    print()
    print("Click this window to make sure it's active, then drive!")

    listener = keyboard.Listener(on_press=on_key_press, on_release=on_key_release)
    listener.start()

    last_command_sent = None

    try:
        while not should_quit:
            command = decide_command()

            if command != last_command_sent:
                labels = {'F': 'FORWARD', 'B': 'BACKWARD',
                          'L': 'TURN LEFT', 'R': 'TURN RIGHT', 'S': 'STOP'}
                log_message(f"Rover action: {labels[command]} (speed: {current_speed_label})")
                last_command_sent = command

            try:
                sock.sendall(command.encode())
            except Exception as e:
                show_error_popup(
                    "Lost connection to rover",
                    f"The connection to the rover dropped unexpectedly.\n\n"
                    f"Error detail: {e}"
                )
                break

            time.sleep(1.0 / SEND_RATE_HZ)

    except KeyboardInterrupt:
        pass

    finally:
        try:
            sock.sendall('S'.encode())
        except Exception:
            pass
        sock.close()
        listener.stop()
        log_message("Disconnected. Rover stopped. Session ended.")
        _popup_root.destroy()


if __name__ == "__main__":
    main()
