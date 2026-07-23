from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request, send_from_directory


PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROJECT_CONFIG_PATH = PROJECT_ROOT / "config" / "project.json"
FIRMWARE_CONFIG_PATH = PROJECT_ROOT / "data" / "flight-controller.json"
WEB_ROOT = PROJECT_ROOT / "web"


def read_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as config_file:
        value = json.load(config_file)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_path = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as config_file:
            json.dump(value, config_file, ensure_ascii=False, indent=2)
            config_file.write("\n")
        os.replace(temporary_path, path)
    except Exception:
        Path(temporary_path).unlink(missing_ok=True)
        raise


def deep_merge(original: dict[str, Any], updates: dict[str, Any]) -> dict[str, Any]:
    merged = dict(original)
    for key, value in updates.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = deep_merge(merged[key], value)
        else:
            merged[key] = value
    return merged


def create_app() -> Flask:
    app = Flask(__name__, static_folder=str(WEB_ROOT), static_url_path="")

    @app.get("/")
    def index():
        return send_from_directory(WEB_ROOT, "index.html")

    @app.get("/api/health")
    def health():
        return jsonify({"status": "ok", "service": "fipik-configurator"})

    @app.get("/api/project")
    def get_project_config():
        return jsonify(read_json(PROJECT_CONFIG_PATH))

    @app.get("/api/config")
    def get_firmware_config():
        return jsonify(read_json(FIRMWARE_CONFIG_PATH))

    @app.put("/api/config")
    def update_firmware_config():
        updates = request.get_json(silent=True)
        if not isinstance(updates, dict):
            return jsonify({"error": "request body must be a JSON object"}), 400

        current = read_json(FIRMWARE_CONFIG_PATH)
        updated = deep_merge(current, updates)
        write_json(FIRMWARE_CONFIG_PATH, updated)
        return jsonify(updated)

    return app


app = create_app()


if __name__ == "__main__":
    app.run(
        host=os.getenv("FIPIK_HOST", "127.0.0.1"),
        port=int(os.getenv("FIPIK_PORT", "5000")),
        debug=False,
    )
