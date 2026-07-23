# Fipik flight controller

Early-stage ESP32-WROOM-32 flight-controller prototype for a quad-X airframe.

## Hardware baseline

- ESP32-WROOM-32
- HAKRC 4-in-1 45A ESC
- GY-271 magnetometer
- 24 V, 8700 mAh battery

An IMU with a three-axis gyroscope and accelerometer is still required before any stabilization or motor-control work.

## Configuration

`config/project.json` is the global project passport: paths, development settings, hardware inventory, safety constraints, and project status.

All adjustable hardware parameters used by the ESP32 firmware are in `data/flight-controller.json`. It is uploaded to LittleFS separately from the firmware:

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

## Local web configurator

The Flask server serves the web configurator and a local REST API. It binds to `127.0.0.1` by default and does not expose the configuration to the network:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python -m server.app
```

Open `http://127.0.0.1:5000/` in a browser.

Available endpoints:

- `GET /api/health` — server status;
- `GET /api/project` — global project configuration;
- `GET /api/config` — firmware configuration;
- `PUT /api/config` — merge and save firmware configuration.
