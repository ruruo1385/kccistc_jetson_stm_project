import re
import socket
import threading
import time
from collections import Counter
from pathlib import Path
from queue import Queue
from time import monotonic

import cv2
from ultralytics import YOLO


HOST = "10.10.16.78"
PORT = 5000
WAIT_SECONDS = 2.0
message_queue = Queue()
stop_event = threading.Event()


def cameraWork():
    model = YOLO(str(Path(__file__).with_name("best.pt")))
    cap = cv2.VideoCapture(0)
    predictions = []
    label = "Measuring..."
    next_measurement = 0.0

    try:
        if not cap.isOpened():
            raise RuntimeError("웹캠을 열 수 없습니다.")

        while not stop_event.is_set():
            ok, frame = cap.read()
            if not ok:
                break

            # 대기 중에도 웹캠 화면은 계속 갱신합니다.
            if monotonic() >= next_measurement:
                result = model.predict(frame, imgsz=224, verbose=False)[0]
                class_name = result.names[result.probs.top1]
                if class_name == "background":
                    predictions.clear()
                    label = "No eraser"
                else:
                    if not predictions:
                        label = "Measuring..."
                    predictions.append((class_name, float(result.probs.top1conf.item())))

                if len(predictions) == 5:
                    label, votes = Counter(name for name, _ in predictions).most_common(1)[0]
                    confidence = sum(score for name, score in predictions if name == label) / votes * 100
                    status = "PASS" if label == "pass" else "FAIL"
                    defect = "NULL" if label == "pass" else label.upper()
                    payload = f"IMAGE@{status}@{defect}@{confidence:.1f}"
                    message = f"[UTW_SQL]{payload}"
                    print(message, flush=True)
                    message_queue.put(message)
                    message_ard = f"[UTW_ARD]{payload}"
                    print(message_ard, flush=True)
                    message_queue.put(message_ard)
                    predictions.clear()
                    next_measurement = monotonic() + WAIT_SECONDS

            cv2.putText(frame, label, (20, 40),
                        cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            cv2.imshow("Eraser Classification", frame)
            if cv2.waitKey(1) & 0xFF in (27, ord("q"), ord("Q")):
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()


def sendingMsg(s):
    try:
        s.sendall(b"[QC_CHK:PASSWD]")
        time.sleep(0.5)
        while not stop_event.is_set():
            message = message_queue.get()  # 판정 결과가 들어올 때까지 대기
            if message is None:
                break
            s.sendall((message + "\n").encode("utf-8"))
    except OSError as e:
        print(f"전송 오류: {e}")
        stop_event.set()


def gettingMsg(s):
    try:
        while not stop_event.is_set():
            data = s.recv(1024)
            if not data:
                break
            print("recv :", re.split(r"[\[\]@\n]", data.decode("utf-8")))
    except OSError:
        pass
    finally:
        stop_event.set()


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        print("connect is success")
        sender = threading.Thread(target=sendingMsg, args=(s,), daemon=True)
        receiver = threading.Thread(target=gettingMsg, args=(s,), daemon=True)
        sender.start()
        receiver.start()
        try:
            cameraWork()
        finally:
            message_queue.put(None)  # 전송 스레드 종료
            sender.join(timeout=2)
            stop_event.set()
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


if __name__ == "__main__":
    main()
