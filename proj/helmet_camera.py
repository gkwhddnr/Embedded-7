from pathlib import Path

import cv2
from picamera2 import Picamera2
from ultralytics import YOLO


def main():
    model_path = Path(__file__).resolve().parent/"best.pt"
    if not model_path.exists():
        raise FileNotFoundError(f"Model file not found: {model_path}")

    model = YOLO(str(model_path))
    print("Model classes:", model.names)

    camera = Picamera2()
    camera.preview_configuration.main.size = (640, 480)
    camera.preview_configuration.main.format = "RGB888"
    camera.preview_configuration.align()
    camera.configure("preview")
    camera.start()

    try:
        while True:
            frame = camera.capture_array()
            results = model.predict(source=frame, conf=0.5, verbose=False)
            cv2.imshow("Helmet detection", results[0].plot())
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        camera.stop()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
