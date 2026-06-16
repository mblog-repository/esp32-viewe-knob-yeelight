#!/usr/bin/env python3
"""Утилита для платы ESP32-C3 (VIEWE knob).
Команды:
  python3 serial_tool.py reset            — аппаратный сброс через линии порта
  python3 serial_tool.py monitor [сек]    — читать Serial (по умолчанию бесконечно)
Порт определяется автоматически (/dev/cu.usbmodem*).
"""
import sys, glob, time

try:
    import serial
except ImportError:
    print("Нет pyserial. Установи: pip3 install pyserial")
    sys.exit(1)


def find_port():
    ports = glob.glob('/dev/cu.usbmodem*')
    return ports[0] if ports else None


def do_reset(port):
    s = serial.Serial(port, 115200, timeout=0.2)
    s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
    s.close()
    print("reset отправлен ->", port)


def do_monitor(port, seconds):
    s = serial.Serial(port, 115200, timeout=0.2)
    print(f"--- monitor {port} (Ctrl+C для выхода) ---")
    deadline = time.time() + seconds if seconds else None
    try:
        while deadline is None or time.time() < deadline:
            try:
                d = s.read(4096)
            except Exception:
                s.close(); time.sleep(0.3)
                p = find_port()
                if p:
                    try: s = serial.Serial(p, 115200, timeout=0.2)
                    except Exception: pass
                continue
            if d:
                sys.stdout.write(d.decode('utf-8', 'replace')); sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n--- стоп ---")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "monitor"
    port = find_port()
    if not port:
        print("Порт не найден (/dev/cu.usbmodem*). Плата подключена?")
        sys.exit(1)
    if cmd == "reset":
        do_reset(port)
    elif cmd == "monitor":
        secs = int(sys.argv[2]) if len(sys.argv) > 2 else 0
        do_monitor(port, secs)
    else:
        print("Неизвестная команда:", cmd)
        sys.exit(1)
