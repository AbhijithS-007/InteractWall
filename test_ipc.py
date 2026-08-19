import json, time

try:
    with open(r'\\.\pipe\InteractWall', 'w') as f:
        print("Sending remove_effect...")
        f.write(json.dumps({'cmd':'remove_effect', 'plugin':''}) + '\n')
        f.flush()
        time.sleep(1)

        print("Sending apply_wallpaper...")
        f.write(json.dumps({'cmd':'apply_wallpaper', 'layerA':'C:\\test\\test.png', 'layerB':''}) + '\n')
        f.flush()
        time.sleep(1)
        print("Done!")
except Exception as e:
    print(f"Error: {e}")
