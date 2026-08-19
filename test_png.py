import ctypes

res = ctypes.windll.user32.SystemParametersInfoW(20, 0, r'C:\Users\abhir\AppData\Roaming\InteractWall\baked_wallpapers\1785504203358.png', 3)
print('Success' if res else 'Fail')
