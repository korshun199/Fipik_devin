from __future__ import annotations

import os
import threading
import time
from dataclasses import asdict, dataclass
from typing import Iterator

import cv2
from flask import Flask, Response, jsonify

from vision import Detection, MotionDetector, find_camera_device


@dataclass
class RuntimeStatus:
    camera: str | None = None
    camera_state: str = "ожидание камеры"
    fps: float = 0.0
    frames: int = 0
    error: str | None = None


class VisionRuntime:
    def __init__(self) -> None:
        self.detector = MotionDetector()
        self.lock = threading.Lock()
        self.jpeg: bytes | None = None
        self.detection = MotionDetector._empty_detection()
        self.status = RuntimeStatus()
        self.running = True
        self.thread = threading.Thread(target=self._run, daemon=True, name="camera")

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.running = False
        self.thread.join(timeout=3)

    def snapshot(self) -> tuple[bytes | None, Detection, RuntimeStatus]:
        with self.lock:
            return self.jpeg, self.detection, RuntimeStatus(**asdict(self.status))

    def _run(self) -> None:
        while self.running:
            camera_device = os.getenv("ROBOT_CAMERA") or find_camera_device()
            if camera_device is None:
                self._set_waiting("камера не подключена")
                time.sleep(2)
                continue

            capture = cv2.VideoCapture(camera_device, cv2.CAP_V4L2)
            capture.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            capture.set(cv2.CAP_PROP_FPS, 25)
            if not capture.isOpened():
                self._set_waiting(f"не удалось открыть {camera_device}")
                capture.release()
                time.sleep(2)
                continue

            self.detector.reset()
            self._set_camera(camera_device)
            frame_count = 0
            period_started = time.monotonic()

            while self.running:
                success, frame = capture.read()
                if not success:
                    self._set_waiting(f"потерян видеопоток {camera_device}")
                    break

                annotated, detection = self.detector.process(frame)
                encoded, jpeg = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 82])
                if not encoded:
                    continue

                frame_count += 1
                now = time.monotonic()
                elapsed = now - period_started
                with self.lock:
                    self.jpeg = jpeg.tobytes()
                    self.detection = detection
                    self.status.frames += 1
                    self.status.error = None
                    if elapsed >= 1.0:
                        self.status.fps = frame_count / elapsed
                        frame_count = 0
                        period_started = now

            capture.release()
            time.sleep(1)

    def _set_waiting(self, message: str) -> None:
        with self.lock:
            self.status.camera = None
            self.status.camera_state = "ожидание камеры"
            self.status.error = message
            self.status.fps = 0.0
            self.jpeg = None
            self.detection = MotionDetector._empty_detection()

    def _set_camera(self, camera_device: str) -> None:
        with self.lock:
            self.status.camera = camera_device
            self.status.camera_state = "камера работает"
            self.status.error = None


app = Flask(__name__)
runtime = VisionRuntime()
runtime.start()


@app.get("/api/status")
def api_status():
    _, detection, status = runtime.snapshot()
    return jsonify({"runtime": asdict(status), "detection": detection.to_dict()})


@app.get("/api/health")
def api_health():
    _, _, status = runtime.snapshot()
    return jsonify({"status": "ok", "camera": status.camera_state})


@app.get("/video")
def video():
    return Response(_mjpeg_stream(), mimetype="multipart/x-mixed-replace; boundary=frame")


def _mjpeg_stream() -> Iterator[bytes]:
    while True:
        jpeg, _, _ = runtime.snapshot()
        if jpeg is None:
            time.sleep(0.2)
            continue
        yield b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + jpeg + b"\r\n"
        time.sleep(0.04)


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=int(os.getenv("ROBOT_PORT", "8000")), threaded=True)
