# Fipik flight controller

Early-stage ESP32-WROOM-32 flight-controller prototype for a quad-X airframe.

## Hardware baseline

- ESP32-WROOM-32
- HAKRC 4-in-1 45A ESC
- GY-271 magnetometer
- 24 V, 8700 mAh battery

An IMU with a three-axis gyroscope and accelerometer is still required before any stabilization or motor-control work.

## Configuration

All adjustable hardware parameters are in `data/flight-controller.json`. It is uploaded to LittleFS separately from the firmware:

```sh
pio run --target uploadfs
```

The current firmware reads and validates the file at startup, then reports the selected hardware over serial. It does not arm the ESC or drive motors.

## Initial workflow

```sh
pio run
pio run --target uploadfs
pio run --target upload
pio device monitor
```
