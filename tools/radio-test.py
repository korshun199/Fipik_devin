#!/usr/bin/env python3
"""Интерактивная проверка каналов RadioMaster через ESP32."""

import argparse
import json
import re
import statistics
import sys
import time

try:
    import serial
except ImportError:
    print("Не найден pyserial. Установи: python3 -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)


CHANNELS_PATTERN = re.compile(r"RC channels:\s+([0-9 ]+)")


def read_channels(port: serial.Serial, seconds: float = 1.5) -> list[int]:
    samples: list[list[int]] = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace")
        match = CHANNELS_PATTERN.search(line)
        if not match:
            continue
        values = [int(value) for value in match.group(1).split()]
        if len(values) >= 16:
            samples.append(values[:16])
    if not samples:
        raise RuntimeError("Не получил строки RC channels от ESP32.")
    return [round(statistics.median(column)) for column in zip(*samples)]


def capture(port: serial.Serial, instruction: str) -> list[int]:
    input(f"\n{instruction}\nКогда стик установлен, нажми Enter...")
    values = read_channels(port)
    print("Получено: " + " ".join(str(value) for value in values))
    return values


def channel_difference(first: list[int], second: list[int], index: int) -> int:
    return abs(second[index] - first[index])


def main() -> int:
    parser = argparse.ArgumentParser(description="Проверка раскладки каналов нового пульта")
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.25)
    except serial.SerialException as error:
        print(f"Не удалось открыть {args.port}: {error}", file=sys.stderr)
        return 1

    print("ESP32 подключён. Аккумулятор отключён, ARM не включать.")
    port.reset_input_buffer()
    measurements = {
        "center": capture(port, "Все стики в центр."),
        "left_up": capture(port, "Левый стик ВВЕРХ — это должен быть газ."),
        "left_down": capture(port, "Левый стик ВНИЗ."),
        "left_left": capture(port, "Левый стик ВЛЕВО — это должен быть разворот."),
        "left_right": capture(port, "Левый стик ВПРАВО."),
        "right_left": capture(port, "Правый стик ВЛЕВО — крен."),
        "right_right": capture(port, "Правый стик ВПРАВО."),
        "right_up": capture(port, "Правый стик ВВЕРХ — тангаж."),
        "right_down": capture(port, "Правый стик ВНИЗ."),
        "arm_off": capture(port, "ARM выключен."),
        "arm_on": capture(port, "ARM включен. Моторы не запустятся: аккумулятор отключён."),
    }

    all_channels = range(16)
    throttle = max(all_channels, key=lambda index: channel_difference(
        measurements["left_up"], measurements["left_down"], index))
    yaw = max((index for index in all_channels if index != throttle), key=lambda index:
              channel_difference(measurements["left_left"], measurements["left_right"], index))
    roll = max((index for index in all_channels if index not in {throttle, yaw}), key=lambda index:
               channel_difference(measurements["right_left"], measurements["right_right"], index))
    pitch = max((index for index in all_channels if index not in {throttle, yaw, roll}), key=lambda index:
                channel_difference(measurements["right_up"], measurements["right_down"], index))
    arm = max(all_channels, key=lambda index: channel_difference(
        measurements["arm_off"], measurements["arm_on"], index))

    result = {
        "roll_channel": roll,
        "pitch_channel": pitch,
        "throttle_channel": throttle,
        "yaw_channel": yaw,
        "arm_channel": arm,
        "measurements": measurements,
    }
    print("\nПредлагаемая раскладка каналов:")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print("\nПришли этот вывод Машеньке — она перенесёт раскладку в основной конфиг.")
    port.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
