import sys
import os
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtCore import QUrl

def main():
    app = QGuiApplication(sys.argv)
    app.setApplicationName("Autonomous Video Editor")
    app.setOrganizationName("AVE")

    engine = QQmlApplicationEngine()
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
    qml_file = os.path.join(script_dir, "qml", "main.qml")
    
    if not os.path.exists(qml_file):
        print(f"Error: QML file not found at {qml_file}", file=sys.stderr)
        sys.exit(1)

    print("======================================================================", flush=True)
    print("      AUTONOMOUS VIDEO EDITOR (AVE) - LAUNCHING NATIVE DESKTOP UI     ", flush=True)
    print("======================================================================", flush=True)
    print(f"[UI Presentation] Loading QML Dark Theme Workspace from: {qml_file}", flush=True)
    
    engine.load(QUrl.fromLocalFile(qml_file))

    if not engine.rootObjects():
        print("Error: Failed to load QML root window.", file=sys.stderr, flush=True)
        sys.exit(1)

    print("[UI Presentation] Desktop Window Active (1440x900).", flush=True)
    print("[UI Presentation] Close the editor window to exit.", flush=True)
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
