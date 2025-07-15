#!/usr/bin/env python3
import os
import sys
import glob

# Check if PIL (Pillow) is installed
try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("\n" + "="*60)
    print("WARNING: Sprite conversion skipped - PIL/Pillow not available")
    print("="*60)
    print("\nThe png2c.py script requires PIL (Pillow) and numpy to convert sprites.")
    print("PlatformIO uses its own Python environment, separate from your system Python.")
    print("\nTo install in PlatformIO's Python environment:")
    print("  ~/.platformio/penv/bin/pip install Pillow numpy")
    print("\nOr on Windows:")
    print("  %USERPROFILE%\\.platformio\\penv\\Scripts\\pip install Pillow numpy")
    print("\nAlternatively, you can run the script manually before building:")
    print("  python3 png2c.py")
    print("\nContinuing build without sprite conversion...")
    print("="*60 + "\n")
    sys.exit(0)  # Exit gracefully to allow build to continue

def png_to_c_array(png_file, output_dir, var_name_override=None):
    """Convert PNG file to LVGL compatible C array"""
    
    # Create a safe C variable name from the filename
    base_name = os.path.splitext(os.path.basename(png_file))[0]
    var_name = var_name_override if var_name_override else base_name.replace('-', '_').replace('.', '_')
    
    # Load the image
    img = Image.open(png_file)
    width, height = img.size
    
    # Convert to RGBA if not already
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # Get pixel data as a numpy array
    pixels = np.array(img)
    
    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)
    
    # Get all PNG files in sequence to determine array indices
    output_c_file = os.path.join(output_dir, f"sprite_{var_name}.c")
    output_h_file = os.path.join(output_dir, f"sprite_{var_name}.h")
    
    # Write C file with pixel data
    with open(output_c_file, 'w') as f:
        f.write(f"""/**
 * @file sprite_{var_name}.c
 * @brief LVGL sprite generated from {os.path.basename(png_file)}
 */

#include "lvgl.h"
#include "sprite_{var_name}.h"

// Image data for {os.path.basename(png_file)}
static const uint8_t sprite_{var_name}_map[] = {{
""")
        
        # Process pixels
        for y in range(height):
            f.write("    ")
            for x in range(width):
                r, g, b, a = pixels[y, x]
                
                # Using BGRA order for LVGL ARGB8888 format (this works!)
                # Each component is 8 bits (1 byte)
                
                # Output as individual bytes in BGRA order
                f.write(f"{b}, {g}, {r}, {a},")
                
                if x < width - 1:
                    f.write(" ")
            f.write("\n")
        
        f.write("};\n\n")
        
        # LVGL image descriptor - updated for LVGL 9.x
        f.write(f"""// LVGL image descriptor
const lv_img_dsc_t sprite_{var_name} = {{
    .header = {{
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .w = {width},
        .h = {height},
    }},
    .data_size = {width * height * 4},
    .data = sprite_{var_name}_map
}};
""")
    
    # Write header file
    with open(output_h_file, 'w') as f:
        f.write(f"""/**
 * @file sprite_{var_name}.h
 * @brief LVGL sprite generated from {os.path.basename(png_file)}
 */

#pragma once

#ifdef __cplusplus
extern "C" {{
#endif

#include "lvgl.h"

// Image descriptor declaration
extern const lv_img_dsc_t sprite_{var_name};

#ifdef __cplusplus
}}
#endif
""")
    
    print(f"Generated {output_c_file} and {output_h_file}")
    return var_name


def main():
    print("PNG to LVGL Sprite Converter")
    print("============================")
    
    
    # Create output directory if it doesn't exist
    output_dir = "include/sprites"
    os.makedirs(output_dir, exist_ok=True)
    
    # Find all PNG files in the raw-png directory (including subdirectories)
    png_files = sorted(glob.glob("raw-png/**/*.png", recursive=True))
    
    if not png_files:
        print("No PNG files found in raw-png directory!")
        sys.exit(1)
    
    print(f"Found {len(png_files)} PNG files to process")
    
    # Group files by subdirectory
    sprites_by_dir = {}
    for png_file in png_files:
        # Get the relative path from raw-png/
        rel_path = os.path.relpath(png_file, 'raw-png')
        # Get the subdirectory (or 'root' if in sprites/ directly)
        parts = rel_path.split(os.sep)
        if len(parts) > 1:
            subdir = parts[0]
        else:
            subdir = 'root'
        
        if subdir not in sprites_by_dir:
            sprites_by_dir[subdir] = []
        sprites_by_dir[subdir].append(png_file)
    
    # Process each subdirectory
    all_sprite_groups = {}
    for subdir, files in sprites_by_dir.items():
        sprite_names = []
        for png_file in files:
            var_name = png_to_c_array(png_file, output_dir)
            if var_name:
                sprite_names.append(var_name)
        if sprite_names:
            all_sprite_groups[subdir] = sprite_names
    
    # Generate sprites.h file that includes all sprite headers
    sprites_h_content = """/**
 * @file sprites.h
 * @brief Includes all LVGL sprite headers and arrays
 */

#ifndef LVGL_SPRITES_H
#define LVGL_SPRITES_H

#ifdef __cplusplus
extern "C" {
#endif

// Include all sprite headers
"""
    # Include all sprite headers from all subdirectories
    for subdir, sprite_names in all_sprite_groups.items():
        for sprite_name in sprite_names:
            sprites_h_content += f'#include "sprite_{sprite_name}.h"\n'
    
    sprites_h_content += "\n"
    
    # Add arrays for each subdirectory
    for subdir in sorted(all_sprite_groups.keys()):
        sprites_h_content += f"// Array of all {subdir} sprites\n"
        sprites_h_content += f"extern const lv_img_dsc_t* {subdir}_sprites[];\n"
        sprites_h_content += f"extern const uint8_t {subdir}_sprites_count;\n\n"
    
    sprites_h_content += """#ifdef __cplusplus
}
#endif

#endif /* LVGL_SPRITES_H */
"""
    
    with open(os.path.join(output_dir, "sprites.h"), 'w') as f:
        f.write(sprites_h_content)
    
    # Generate sprites.c file with the sprite arrays
    sprites_c_content = """/**
 * @file sprites.c
 * @brief Contains arrays of sprite pointers for each subdirectory
 */

#include "sprites.h"

"""
    
    # Create arrays for each subdirectory
    for subdir in sorted(all_sprite_groups.keys()):
        sprite_names = all_sprite_groups[subdir]
        
        sprites_c_content += f"// Array of all {subdir} sprites\n"
        sprites_c_content += f"const lv_img_dsc_t* {subdir}_sprites[] = {{\n"
        
        for sprite_name in sprite_names:
            sprites_c_content += f'    &sprite_{sprite_name},\n'
        
        sprites_c_content += f"}};\n\n"
        sprites_c_content += f"// Number of sprites in the {subdir} animation\n"
        sprites_c_content += f"const uint8_t {subdir}_sprites_count = sizeof({subdir}_sprites) / sizeof({subdir}_sprites[0]);\n\n"
    
    with open(os.path.join(output_dir, "sprites.c"), 'w') as f:
        f.write(sprites_c_content)
    
    print(f"Generated {output_dir}/sprites.h and {output_dir}/sprites.c")
    
    # Count total sprites and print summary
    total_sprites = sum(len(sprites) for sprites in all_sprite_groups.values())
    print(f"\nSuccessfully processed {total_sprites} sprites:")
    for subdir in sorted(all_sprite_groups.keys()):
        print(f"  - {subdir}: {len(all_sprite_groups[subdir])} sprites")

# When run as a PlatformIO script
try:
    # PlatformIO's Import function is only available in their environment
    Import("env")  # noqa: F821
    # Run the conversion when called by PlatformIO
    main()
except NameError:
    # When run standalone
    if __name__ == "__main__":
        main() 