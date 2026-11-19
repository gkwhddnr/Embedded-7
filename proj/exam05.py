from ultralytics import YOLO
model = YOLO("yolo26n_ncnn_model") # NCNN 모델 로드 (1회)
results = model("desk.jpg", conf=0.5, verbose=False) # 추론 (verbose: 실행 과정 출력 여부)
for box in results[0].boxes: # 검출된 물체마다 (사진 1장 → results[0])
    name = model.names[int(box.cls[0])] # 클래스 번호 → 이름
    conf = float(box.conf[0]) # 신뢰도 (0~1)
    x1, y1, x2, y2 = map(int, box.xyxy[0]) # 박스 좌표 (픽셀)
    print(f"{name:12s} {conf:.2f} ({x1},{y1})-({x2},{y2})")
