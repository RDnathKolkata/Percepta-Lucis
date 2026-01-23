from ultralytics import YOLO
import cv2

# Load the model
model = YOLO('yolo11m.pt')

# Test with an image (replace with your test image path)
results = model('istockphoto-540090804-612x612.jpg')

# Display results
results[0].show()

# Print detections
for result in results:
    boxes = result.boxes
    for box in boxes:
        cls = int(box.cls[0])
        conf = float(box.conf[0])
        name = model.names[cls]
        print(f"Detected: {name} with confidence {conf:.2f}")
