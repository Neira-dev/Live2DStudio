"""
MediaPipe Face Landmarker のモデルを ONNX に変換して App/Tracking/Models に置く。

使い方（一度だけ実行。TensorFlow など約 500MB をインストールするので仮想環境を推奨）:
    python -m venv .venv-convert
    .venv-convert\Scripts\python -m pip install "tensorflow-cpu==2.17.*" "tf2onnx==1.16.1" "onnx==1.16.*"
    .venv-convert\Scripts\python tools/convert_mediapipe_models.py

モデル: https://ai.google.dev/edge/mediapipe/solutions/vision/face_landmarker （Apache License 2.0）
"""
import os
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

import onnx

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task"
MODELS = ["face_detector", "face_landmarks_detector", "face_blendshapes"]
OPSET = 13  # Windows に標準で入っている Windows ML が扱える版

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(ROOT, "App", "Tracking", "Models")


def fix_batch_dimension(path):
    """可変のバッチ次元を 1 に固定する（Windows ML で扱いやすくするため）"""
    model = onnx.load(path)
    for value in list(model.graph.input) + list(model.graph.output):
        dims = value.type.tensor_type.shape.dim
        if dims and not dims[0].dim_value:
            dims[0].ClearField("dim_param")
            dims[0].dim_value = 1
    onnx.save(model, path)


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        task_path = os.path.join(work, "face_landmarker.task")
        print("download:", MODEL_URL)
        urllib.request.urlretrieve(MODEL_URL, task_path)
        with zipfile.ZipFile(task_path) as archive:
            archive.extractall(work)
        for name in MODELS:
            output = os.path.join(OUTPUT_DIR, name + ".onnx")
            subprocess.check_call([sys.executable, "-m", "tf2onnx.convert", "--tflite", os.path.join(work, name + ".tflite"),
                                   "--output", output, "--opset", str(OPSET)])
            fix_batch_dimension(output)
            print("saved:", output)


if __name__ == "__main__":
    main()
