import re
import glob

files = glob.glob('C:/My_Proj/InteractWall/ui/src/pages/*.tsx')
for file in files:
    with open(file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    def replacer(match):
        step_val = float(match.group(1))
        if step_val >= 1:
            new_step = '0.1'
        elif step_val >= 0.1:
            new_step = '0.01'
        else:
            new_step = '0.001'
        return f'step="{new_step}"'
    
    new_content = re.sub(r'step="([0-9.]+)"', replacer, content)
    with open(file, 'w', encoding='utf-8') as f:
        f.write(new_content)
print('Sliders patched!')
