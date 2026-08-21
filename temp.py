import win32file  
import json  
handle = win32file.CreateFile(r\" "\\.\pipe\InteractWall\, win32file.GENERIC_READ | win32file.GENERIC_WRITE, 0, None, win32file.OPEN_EXISTING, 0, None)  
win32file.WriteFile(handle, b\" -encodedCommand XABcAFwAIgBjAG0AZABcAFwAXAAiADoAIABcAFwAXAAiAGcAZQB0AF8AcwB0AGEAdAB1AHMAXABcAFwAIgA= "\n\)  
print(win32file.ReadFile(handle, 4096)[1].decode())  
