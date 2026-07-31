from __future__ import annotations

from collections import deque
from dataclasses import asdict, dataclass
from pathlib import Path
from time import monotonic

import cv2
import numpy as np


@dataclass(frozen=True)
class Detection:
    detected: bool
    horizontal: str
    depth: str
    center_x: int | None
    center_y: int | None
    width: int | None
    height: int | None
    area_ratio: float
    foreground_ratio: float

    def to_dict(self) -> dict[str, bool | str | int | float | None]:
        return asdict(self)


class MotionDetector:
    def __init__(
        self,
        background_alpha: float = 0.025,
        threshold: int = 28,
        minimum_area_ratio: float = 0.006,
        history_size: int = 8,
    ) -> None:
        self.background_alpha = background_alpha
        self.threshold = threshold
        self.minimum_area_ratio = minimum_area_ratio
        self.background: np.ndarray | None = None
        self.history: deque[tuple[float, float, float]] = deque(maxlen=history_size)
        self.kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))

    def reset(self) -> None:
        self.background = None
        self.history.clear()

    def process(self, frame: np.ndarray) -> tuple[np.ndarray, Detection]:
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (7, 7), 0)

        if self.background is None:
            self.background = gray.astype(np.float32)
            return frame.copy(), self._empty_detection()

        background_u8 = cv2.convertScaleAbs(self.background)
        difference = cv2.absdiff(gray, background_u8)
        _, mask = cv2.threshold(difference, self.threshold, 255, cv2.THRESH_BINARY)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, self.kernel, iterations=1)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, self.kernel, iterations=2)
        mask = cv2.dilate(mask, self.kernel, iterations=1)

        frame_area = frame.shape[0] * frame.shape[1]
        minimum_area = frame_area * self.minimum_area_ratio
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        contours = [contour for contour in contours if cv2.contourArea(contour) >= minimum_area]
        foreground_ratio = float(cv2.countNonZero(mask) / frame_area)

        background_mask = cv2.bitwise_not(mask)
        cv2.accumulateWeighted(gray, self.background, self.background_alpha, mask=background_mask)

        if not contours:
            self.history.clear()
            return frame.copy(), self._empty_detection(foreground_ratio)

        contour = max(contours, key=cv2.contourArea)
        x, y, width, height = cv2.boundingRect(contour)
        center_x = x + width // 2
        center_y = y + height // 2
        area_ratio = float((width * height) / frame_area)
        now = monotonic()
        self.history.append((now, center_x / frame.shape[1], area_ratio))
        horizontal, depth = self._classify_motion()

        annotated = frame.copy()
        cv2.rectangle(annotated, (x, y), (x + width, y + height), (80, 220, 120), 2)
        cv2.circle(annotated, (center_x, center_y), 4, (50, 150, 255), -1)
        cv2.putText(
            annotated,
            f"{horizontal}; {depth}",
            (max(8, x), max(24, y - 10)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.62,
            (80, 220, 120),
            2,
            cv2.LINE_AA,
        )

        return annotated, Detection(
            detected=True,
            horizontal=horizontal,
            depth=depth,
            center_x=center_x,
            center_y=center_y,
            width=width,
            height=height,
            area_ratio=area_ratio,
            foreground_ratio=foreground_ratio,
        )

    def _classify_motion(self) -> tuple[str, str]:
        if len(self.history) < 4:
            return "ожидание", "ожидание"

        _, start_x, start_area = self.history[0]
        _, end_x, end_area = self.history[-1]
        horizontal_delta = end_x - start_x
        if horizontal_delta > 0.025:
            horizontal = "движется вправо"
        elif horizontal_delta < -0.025:
            horizontal = "движется влево"
        else:
            horizontal = "по центру"

        area_change = end_area / max(start_area, 1e-6)
        if area_change > 1.14:
            depth = "приближается"
        elif area_change < 0.88:
            depth = "удаляется"
        else:
            depth = "дистанция стабильна"

        return horizontal, depth

    @staticmethod
    def _empty_detection(foreground_ratio: float = 0.0) -> Detection:
        return Detection(
            detected=False,
            horizontal="объект не обнаружен",
            depth="объект не обнаружен",
            center_x=None,
            center_y=None,
            width=None,
            height=None,
            area_ratio=0.0,
            foreground_ratio=foreground_ratio,
        )


def find_camera_device() -> str | None:
    for video_path in sorted(Path("/dev").glob("video*")):
        name_path = Path("/sys/class/video4linux") / video_path.name / "name"
        device_name = name_path.read_text(encoding="utf-8").strip().lower() if name_path.exists() else ""
        if any(value in device_name for value in ("decoder", "encoder", "venus")):
            continue
        return str(video_path)
    return None
