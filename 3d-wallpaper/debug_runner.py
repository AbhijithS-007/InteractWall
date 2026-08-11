import ctypes
import subprocess
import struct
import time

DEBUG_PROCESS = 0x00000001
INFINITE = 0xFFFFFFFF
DBG_CONTINUE = 0x00010002
CREATE_NEW_CONSOLE = 0x00000010

class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [
        ("dwDebugEventCode", ctypes.c_uint32),
        ("dwProcessId", ctypes.c_uint32),
        ("dwThreadId", ctypes.c_uint32),
        ("u", ctypes.c_byte * 160), # Union padding
    ]

kernel32 = ctypes.windll.kernel32

si = ctypes.create_string_buffer(104)
pi = ctypes.create_string_buffer(24)

# Run process
cmd = b"build\\Release\\Scene3DViewer.exe"
if not kernel32.CreateProcessA(None, cmd, None, None, False, DEBUG_PROCESS | CREATE_NEW_CONSOLE, None, b"C:\\My_Proj\\InteractWall\\3d-wallpaper", ctypes.byref(si), ctypes.byref(pi)):
    print("Failed to start process")
    exit(1)

pid = struct.unpack("I", pi.raw[8:12])[0]
print(f"Started PID: {pid}")

start_time = time.time()
sent_close = False

# Debug loop
event = DEBUG_EVENT()
while True:
    if not kernel32.WaitForDebugEvent(ctypes.byref(event), 100):
        if time.time() - start_time > 3.0 and not sent_close:
            # Post WM_CLOSE to gracefully exit and trigger ReportLiveObjects
            HWND = ctypes.windll.user32.FindWindowA(b'Scene3DViewerClass', b'3D Wallpaper')
            if HWND:
                ctypes.windll.user32.PostMessageA(HWND, 0x0010, 0, 0)
                sent_close = True
        continue
    
    code = event.dwDebugEventCode
    
    if code == 5: # EXIT_PROCESS_DEBUG_EVENT
        print("Process exited")
        kernel32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE)
        break
    elif code == 8: # OUTPUT_DEBUG_STRING_EVENT
        # Read from union
        addr = struct.unpack("Q", event.u[0:8])[0]
        nSize = struct.unpack("H", event.u[8:10])[0]
        
        buf = ctypes.create_string_buffer(nSize)
        bytesRead = ctypes.c_size_t()
        
        proc = kernel32.OpenProcess(0x0010, False, event.dwProcessId) # PROCESS_VM_READ
        if proc:
            kernel32.ReadProcessMemory(proc, addr, buf, nSize, ctypes.byref(bytesRead))
            kernel32.CloseHandle(proc)
            try:
                s = buf.value.decode('utf-8', errors='replace')
                print(s, end="")
            except Exception:
                pass

    kernel32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE)
