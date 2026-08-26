import sys
from PyQt5.QtWidgets import QApplication
# Importiert das Fenster aus der uds_gui.py
from uds_gui import UdsGuiTesterWindow

def main():
    # Initialisiert die PyQt5-Laufzeitumgebung
    app = QApplication(sys.argv)
    
    # Erstellt die Instanz des erweiterten Test-Fensters
    window = UdsGuiTesterWindow()
    window.show()
    
    # Startet die Haupt-Eventschleife der GUI
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()
