import re

def clean_hpgl_string(raw_hpgl, target_min=500, target_max=2500):
    # Remove whitespace and split into individual commands
    raw_hpgl = raw_hpgl.replace('\n', '').replace('\r', '').replace(' ', '')
    commands = [c.strip() for c in raw_hpgl.split(';') if c.strip()]
    
    parsed_points = []
    
    # Extract just the coordinates to find the bounding box
    for cmd in commands:
        opcode = cmd[:2].upper()
        args = cmd[2:]
        if opcode in ['PU', 'PD']:
            coords = [int(n) for n in re.findall(r'-?\d+', args)]
            for i in range(0, len(coords) - 1, 2):
                parsed_points.append((opcode, coords[i], coords[i+1]))
                
    if not parsed_points:
        return "IN;PU0,0;"

    all_x = [p[1] for p in parsed_points]
    all_y = [p[2] for p in parsed_points]
    
    min_x, max_x = min(all_x), max(all_x)
    min_y, max_y = min(all_y), max(all_y)
    
    range_x = max_x - min_x if max_x != min_x else 1
    range_y = max_y - min_y if max_y != min_y else 1
    
    # --- FIX 1: Uniform Scaling to maintain Aspect Ratio ---
    target_range = target_max - target_min
    scale = target_range / max(range_x, range_y)
    
    # Calculate offsets to center the plot in the bounds
    offset_x = target_min + (target_range - (range_x * scale)) / 2
    offset_y = target_min + (target_range - (range_y * scale)) / 2
    
    out_cmds = ["IN"]
    last_pt = None
    
    for opcode, x, y in parsed_points:
        scaled_x = int(offset_x + (x - min_x) * scale)
        scaled_y = int(offset_y + (y - min_y) * scale)
        
        # Skip duplicate consecutive coordinates
        if (opcode, scaled_x, scaled_y) == last_pt:
            continue
            
        out_cmds.append(f"{opcode}{scaled_x},{scaled_y}")
        last_pt = (opcode, scaled_x, scaled_y)
        
    out_cmds.append("PU0,0")
    
    # --- FIX 2: Removed "hpgl " prefix so the C parser hits 'IN' directly ---
    return ";".join(out_cmds) + ";"

# Test raw file
raw_input = """
BP5,1;IN;SP1;IW;PS9600,7100;TR0;IP0,0,7100,9600;SC;
PC1,0,0,0;
PU0,652123;
PD0,0,585216,0,585216,652123,0,652123;
PU135226,488268;
PD135226,186035,441826,186035,441826,488268,135226,488268;
"""

print(clean_hpgl_string(raw_input))