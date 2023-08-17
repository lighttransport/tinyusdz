
import os, sys

# workaround.
# import PySide6 first, then import numpy, cv2, etc. otherwise 
#
# qt.qpa.wayland: Failed to initialize EGL display 3001
#
# error happens in WSL2 environment

from PySide6.QtWidgets import QApplication, QWidget, QLabel
from PySide6.QtGui import QImage, QPixmap

#import cv2
import numpy as np


class MainWindow(QWidget):

   def __init__(self, parent=None):
       super().__init__(parent)

       self.setup()
   
   def setup(self):

        self.width = 512
        self.height = 512

        # HWC
        img = np.zeros((self.height, self.width, 3)).astype(np.uint8)
    
        for x in range(self.width):
            for y in range(self.height):
                img[x,y,0] = x % 256
                img[x,y,1] = y % 256
                img[x,y,2] = 127

        stride = img.strides[0]

        qimg = QImage(img.data, self.width, self.height, stride, QImage.Format.Format_RGB888)       
        pixmap = QPixmap(QPixmap.fromImage(qimg))
        imgLabel = QLabel(self)
        imgLabel.setPixmap(pixmap)
        
        self.resize(imgLabel.pixmap().size())
        self.setWindowTitle("TinyUSDZ viewer")


if __name__ == '__main__':
    app = QApplication([])
    window = MainWindow()
    window.show()
    app.exec()
    
