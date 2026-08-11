import json, struct, sys

def analyze_glb(path):
    with open(path, 'rb') as f:
        magic, version, length = struct.unpack('<III', f.read(12))
        if magic != 0x46546C67:
            print("Not a GLB")
            return
            
        json_len, chunk_type = struct.unpack('<II', f.read(8))
        json_data = f.read(json_len).decode('utf-8')
        doc = json.loads(json_data)
        
        images = doc.get('images', [])
        print(f"Total images: {len(images)}")
        
analyze_glb("C:/My_Proj/InteractWall/3d-wallpaper/assets/911.glb")
