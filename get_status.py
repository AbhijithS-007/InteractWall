import win32file, json
h = win32file.CreateFile(r'\\.\pipe\InteractWall', win32file.GENERIC_READ | win32file.GENERIC_WRITE, 0, None, win32file.OPEN_EXISTING, 0, None)
win32file.WriteFile(h, b'{"cmd": "get_status"}\n')
_, r = win32file.ReadFile(h, 4096)
print(r.decode())
