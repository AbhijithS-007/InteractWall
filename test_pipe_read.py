import json, time

try:
    with open(r'\\.\pipe\InteractWall', 'r+') as f:
        print("Sending apply_wallpaper...")
        f.write(json.dumps({'cmd':'apply_wallpaper', 'layerA':'C:\\test\\test.png', 'layerB':''}) + '\n')
        f.flush()
        
        response = f.readline()
        print(f"Response: {response}")
except Exception as e:
    print(f"Error: {e}")
