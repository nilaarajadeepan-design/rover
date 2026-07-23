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
import select
import time
from datetime import datetime
import tkinter as tk
from tkinter import messagebox
from pynput import keyboard

# ============================= SETTINGS =============================
# Fill in the IP address your ESP32 printed to the Serial Monitor.
# It will look something like "192.168.1.45"
ROVER_IP = "PUT_ROVER_IP_ADDRESS_HERE"

# This must match the SERVER_PORT number in the ESP32 code (8888).
ROVER_PORT = 8888

# How many times per second we send a command while a key is held.
# 10 times per second feels responsive without overwhelming the WiFi.
SEND_RATE_HZ = 10

# Every log line also gets saved to this text file, in the same folder
# you run the script from. Great for showing judges a record of a test run.
LOG_FILE_NAME = "rover_log.txt"

# Maps the number key you press to a human-readable speed name.
# These match the '1'/'2'/'3' cases the ESP32 code is listening for.
SPEED_NAMES = {'1': 'SLOW', '2': 'MEDIUM', '3': 'FAST'}

# Pressing this key sends a manual "reset distance to 0" command,
# matching the '0' case the ESP32 code listens for.
RESET_KEY = '0'

# Keeps track of the speed currently selected, just so our log
# messages can show it. Starts matching the ESP32's default (MEDIUM).
current_speed_label = "MEDIUM"

# This will hold our network connection once main() creates it.
# It's declared here (global) so both main() and on_key_press() can use it.
sock = None

# ============================= DISTANCE TRACKING =============================
# The ESP32 sends us text messages like "DIST:123.4\n" a few times per
# second. Since network data can arrive in odd-sized chunks (not always
# neatly split at each message), we keep a running "buffer" string and
# only process a message once we see the newline character marking its end.
incoming_buffer = ""

# The rover's total distance traveled so far, in millimeters (matches
# what the ESP32 sends). Updated every time a new DIST message arrives.
current_distance_mm = 0.0

# Remembers the last distance value we actually WROTE to the log, so we
# only log a new line when the rover has moved a meaningful amount
# (at least 1 cm) instead of flooding the log file 5 times per second.
last_logged_distance_mm = 0.0


# ============================= LOGGING HELPER =============================
# tkinter needs one hidden "root" window to exist before popups will work,
# even though we never show this root window itself.
_popup_root = tk.Tk()
_popup_root.withdraw()  # hide the blank root window - we only want popups


def log_message(text):
    """
    Writes a message BOTH to the screen (Terminal) and to a log file,
    with a timestamp in front of it, like: [14:32:07] Key pressed: UP
    This gives us a running written record of everything that happened.
    """
    timestamp = datetime.now().strftime("%H:%M:%S")
    full_line = f"[{timestamp}] {text}"

    print(full_line)

    # "a" means "append" - add to the end of the file instead of
    # erasing what's already there.
    with open(LOG_FILE_NAME, "a") as log_file:
        log_file.write(full_line + "\n")


def show_error_popup(title, message):
    """
    Pops up a real error window on screen (not just text in the
    Terminal), AND logs the same error to the screen/file.
    """
    log_message(f"ERROR - {title}: {message}")
    messagebox.showerror(title, message)


def check_for_distance_updates():
    """
    Checks whether the rover has sent us any new data over WiFi, WITHOUT
    pausing/freezing the program to wait for it (that's what "select"
    below does - it just peeks and says "yes there's data" or "no, move on").

    Returns True if everything's fine, or False if the connection appears
    to have been lost (so main() knows to stop the program).
    """
    global incoming_buffer, current_distance_mm, last_logged_distance_mm

    # Ask the operating system: "is there any data waiting on this
    # connection RIGHT NOW?" The "0" means "don't wait even a moment -
    # just tell me immediately either way."
    readable, _, _ = select.select([sock], [], [], 0)

    if sock not in readable:
        return True  # nothing new right now - totally normal, keep going

    try:
        chunk = sock.recv(4096)
    except Exception as e:
        show_error_popup(
            "Lost connection to rover",
            f"Could not read data from the rover.\n\nError detail: {e}"
        )
        return False

    if not chunk:
        # An empty result here means the rover closed the connection.
        show_error_popup(
            "Lost connection to rover",
            "The rover closed the connection unexpectedly."
        )
        return False

    # Add whatever we just received onto our buffer, then pull out any
    # COMPLETE messages (ones that end in a newline character).
    incoming_buffer += chunk.decode(errors="ignore")

    while "\n" in incoming_buffer:
        line, incoming_buffer = incoming_buffer.split("\n", 1)
        line = line.strip()

        if line.startswith("DIST:"):
            try:
                current_distance_mm = float(line[5:])
            except ValueError:
                pass  # ignore anything garbled, just skip it

            # Only write a new log line once the rover has moved at
            # least 1 cm since the last time we logged distance -
            # keeps the log file readable instead of flooded with
            # near-identical numbers 5 times a second.
            if abs(current_distance_mm - last_logged_distance_mm) >= 10:
                distance_cm = current_distance_mm / 10.0
                log_message(f"Distance traveled: {distance_cm:.1f} cm")
                last_logged_distance_mm = current_distance_mm

    return True


# ============================= KEEP TRACK OF HELD KEYS =============================
# This "set" will contain whichever arrow keys are currently pressed down.
# A set automatically avoids duplicates, which is perfect for this.
pressed_keys = set()

# This flag becomes True when we want the whole program to stop
# (for example, when ESC is pressed).
should_quit = False


def on_key_press(key):
    """
    This function runs automatically every time ANY key is pressed.
    We care about the 4 arrow keys, ESC, and the 1/2/3 speed keys.
    """
    global should_quit

    if key == keyboard.Key.esc:
        should_quit = True
        return False  # returning False stops the keyboard listener

    if key in (keyboard.Key.up, keyboard.Key.down,
               keyboard.Key.left, keyboard.Key.right):
        pressed_keys.add(key)
        log_message(f"Key pressed: {key_name(key)}")
        return

    # Number keys (1, 2, 3) come through as a different kind of key
    # object than the arrows - they have a ".char" attribute holding
    # the actual character typed. Arrow keys don't have this, so we
    # check for it safely before using it.
    if hasattr(key, "char") and key.char in SPEED_NAMES:
        set_speed(key.char)
        return

    # The "0" key manually resets distance tracking back to 0 - handy
    # for starting a fresh measurement without needing to power-cycle
    # the whole rover.
    if hasattr(key, "char") and key.char == RESET_KEY:
        reset_distance()


def reset_distance():
    """
    Sends the manual reset command ('0') to the rover, telling it to
    zero out its distance tracking right now. Also resets our own
    local copy of the distance number, so the log/display matches.
    """
    global current_distance_mm, last_logged_distance_mm
    current_distance_mm = 0.0
    last_logged_distance_mm = 0.0
    log_message("Distance manually reset to 0.")

    try:
        sock.sendall(RESET_KEY.encode())
    except Exception as e:
        show_error_popup(
            "Lost connection to rover",
            f"Could not send reset command.\n\nError detail: {e}"
        )


def set_speed(number_key):
    """
    Sends a speed-selection command ('1', '2', or '3') to the rover
    right away - this is a one-time action, not something you hold
    down like the arrow keys, so it's sent immediately on key press.
    """
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
    """
    This function runs automatically every time ANY key is released.
    We remove that key from our "currently held" set.
    """
    if key in pressed_keys:
        pressed_keys.discard(key)
        log_message(f"Key released: {key_name(key)}")


def key_name(key):
    """
    Turns a pynput key object (like keyboard.Key.up) into a plain,
    readable word for our logs (like "UP"). Just makes the log file
    nicer to read.
    """
    names = {
        keyboard.Key.up: "UP",
        keyboard.Key.down: "DOWN",
        keyboard.Key.left: "LEFT",
        keyboard.Key.right: "RIGHT",
    }
    return names.get(key, str(key))


def decide_command():
    """
    Looks at which arrow keys are currently held down and decides
    which single-character command to send to the rover.

    NOTE: If you hold Up+Left at the same time, this simple version
    just picks Up. Diagonal/combined driving is a fun upgrade for later!
    """
    if keyboard.Key.up in pressed_keys:
        return 'F'  # Forward
    elif keyboard.Key.down in pressed_keys:
        return 'B'  # Backward
    elif keyboard.Key.left in pressed_keys:
        return 'L'  # Turn left
    elif keyboard.Key.right in pressed_keys:
        return 'R'  # Turn right
    else:
        return 'S'  # Nothing held down -> Stop


def main():
    global sock

    log_message("=" * 50)
    log_message("ROVER REMOTE CONTROL - session started")
    log_message("=" * 50)
    log_message(f"Connecting to rover at {ROVER_IP}:{ROVER_PORT} ...")

    # Create a socket (a network connection) and connect it to the rover.
    # socket.AF_INET means "use a normal IP address"
    # socket.SOCK_STREAM means "use a reliable TCP connection"
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
    print("  0           = Reset distance tracker to 0")
    print("  ESC         = Quit")
    print()
    print("Distance traveled will be logged automatically as you drive.")
    print("Click this window to make sure it's active, then drive!")

    # Start listening for key presses/releases in the background.
    # This runs separately from our main loop below, so we can keep
    # sending commands AND watch for new key presses at the same time.
    listener = keyboard.Listener(on_press=on_key_press, on_release=on_key_release)
    listener.start()

    last_command_sent = None

    try:
        while not should_quit:
            command = decide_command()

            # Only log when the command actually changes, so we don't
            # spam the log with the same word 10 times a second.
            if command != last_command_sent:
                labels = {'F': 'FORWARD', 'B': 'BACKWARD',
                          'L': 'TURN LEFT', 'R': 'TURN RIGHT', 'S': 'STOP'}
                log_message(f"Rover action: {labels[command]} (speed: {current_speed_label})")
                last_command_sent = command

            # Send the single-character command to the rover over WiFi.
            try:
                sock.sendall(command.encode())
            except Exception as e:
                show_error_popup(
                    "Lost connection to rover",
                    f"The connection to the rover dropped unexpectedly.\n\n"
                    f"Error detail: {e}"
                )
                break

            # Wait a short moment before checking/sending again.
            # This controls how many commands per second we send.
            time.sleep(1.0 / SEND_RATE_HZ)

            # Check if the rover has sent us any distance updates.
            if not check_for_distance_updates():
                break  # connection was lost - stop the program

    except KeyboardInterrupt:
        # This lets you also quit with Ctrl+C in the terminal.
        pass

    finally:
        # Always try to stop the rover and close the connection cleanly
        # when the program ends, so it doesn't keep driving.
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
