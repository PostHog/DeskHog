#!/usr/bin/env python3
import os
import sys
import glob

# Check if PIL (Pillow) is installed
try:
    from PIL import Image
    import numpy as np
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

def process_png_file(png_file_path, output_directory, variable_name_override=None, is_for_walking_array=True):
    """Converts a single PNG file to LVGL compatible C array and header file.
    
    Args:
        png_file_path: Path to the source PNG file.
        output_directory: Directory where the .c and .h files will be saved.
        variable_name_override: If provided, use this as the C variable name.
        is_for_walking_array: (Not used in this function directly but for caller logic)
                              Indicates if this sprite is part of the walking animation.
                              This function now primarily focuses on file generation.

    Returns:
        A tuple (variable_name, generated_h_filename) or (None, None) if failed.
    """
    base_name_no_ext = os.path.splitext(os.path.basename(png_file_path))[0]
    var_name = variable_name_override if variable_name_override else base_name_no_ext.replace('-', '_').replace('.', '_')
    
    os.makedirs(output_directory, exist_ok=True)
    output_h_filename = f"sprite_{var_name}.h"
    output_c_filename = f"sprite_{var_name}.c"
    output_h_filepath = os.path.join(output_directory, output_h_filename)
    output_c_filepath = os.path.join(output_directory, output_c_filename)

    if not HAS_PIL:
        print(f"Skipping {png_file_path} - PIL (Pillow) and numpy are required. Generating DUMMY sprite.")
        with open(output_h_filepath, 'w') as f:
            f.write(f"""/** @file {output_h_filename} @brief Dummy LVGL sprite for {os.path.basename(png_file_path)} (PIL not found) */
#pragma once
#ifdef __cplusplus
extern "C" {{
#endif
#include "lvgl.h"
extern const lv_img_dsc_t sprite_{var_name};
#ifdef __cplusplus
}}
#endif
""")
        with open(output_c_filepath, 'w') as f:
            f.write(f"""/** @file {output_c_filename} @brief Dummy LVGL sprite for {os.path.basename(png_file_path)} (PIL not found) */
#include "lvgl.h"
#include "{output_h_filename}" // Include its own header
static const uint8_t sprite_{var_name}_map[] = {{0x00, 0x00, 0xFF, 0xFF}}; // Minimal 1x1 red pixel
const lv_img_dsc_t sprite_{var_name} = {{ .header = {{ .cf = LV_COLOR_FORMAT_ARGB8888, .w = 1, .h = 1 }}, .data_size = 4, .data = sprite_{var_name}_map }};
""")
        print(f"Generated DUMMY {output_c_filepath} and {output_h_filepath}")
        return var_name, output_h_filename

    img = Image.open(png_file_path)
    width, height = img.size
    img = img.convert('RGBA') # Ensure RGBA
    pixels = np.array(img)
    
    with open(output_c_filepath, 'w') as f:
        f.write(f"""/** @file {output_c_filename} @brief LVGL sprite from {os.path.basename(png_file_path)} */
#include "lvgl.h"
#include "{output_h_filename}" // Include its own header

static const uint8_t sprite_{var_name}_map[] = {{
""")
        for y_coord in range(height):
            f.write("    ")
            for x_coord in range(width):
                r_val, g_val, b_val, a_val = pixels[y_coord, x_coord]
                f.write(f"0x{b_val:02x}, 0x{g_val:02x}, 0x{r_val:02x}, 0x{a_val:02x},") # BGRA hex format
                if x_coord < width - 1:
                    f.write(" ")
            f.write("\n")
        f.write("};\n\n")
        f.write(f"""const lv_img_dsc_t sprite_{var_name} = {{
    .header = {{ .cf = LV_COLOR_FORMAT_ARGB8888, .w = {width}, .h = {height} }},
    .data_size = {width * height * 4}, .data = sprite_{var_name}_map }};
""")
    
    with open(output_h_filepath, 'w') as f:
        f.write(f"""/** @file {output_h_filename} @brief LVGL sprite from {os.path.basename(png_file_path)} */
#pragma once
#ifdef __cplusplus
extern "C" {{
#endif
#include "lvgl.h"
extern const lv_img_dsc_t sprite_{var_name};
#ifdef __cplusplus
}}
#endif
""")
    print(f"Generated {output_c_filepath} and {output_h_filepath}")
    return var_name, output_h_filename

def main():
    print("PNG to LVGL Sprite Converter - v4 (Corrected String Formatting)")
    print("===============================================================")

    if not HAS_PIL:
        print("\nWARNING: Python Imaging Library (PIL/Pillow) and numpy are not installed.")
        print("The script will create placeholder sprites instead of converting real images.")
        print("To install: python3 -m pip install --user pillow numpy\n")

    base_include_dir = "include/sprites"

    # --- Walking Sprites ---
    walking_sprites_output_dir = base_include_dir 
    walking_png_source_dir = "sprites/walking"
    walking_sprite_details = [] # List of (var_name, header_filename)

    os.makedirs(walking_sprites_output_dir, exist_ok=True)
    walking_png_files = sorted(glob.glob(os.path.join(walking_png_source_dir, "*.png")))

    if not walking_png_files:
        print(f"No PNG files found in {walking_png_source_dir} directory.")
    else:
        print(f"Processing {len(walking_png_files)} walking sprites from {walking_png_source_dir}...")
        for png_file in walking_png_files:
            var_name, header_filename = process_png_file(png_file, walking_sprites_output_dir)
            if var_name:
                walking_sprite_details.append((var_name, header_filename))

    # --- Logo Sprite (Separate) ---
    logo_sprite_output_dir = os.path.join(base_include_dir, "logo") # include/sprites/logo
    logo_png_source_file = "sprites/logo/posthog-logo-white.png"
    logo_var_name = None 

    if os.path.exists(logo_png_source_file):
        print(f"Processing logo sprite: {logo_png_source_file}...")
        os.makedirs(logo_sprite_output_dir, exist_ok=True)
        logo_var_name, _ = process_png_file(logo_png_source_file, logo_sprite_output_dir, "posthog_logo_white") 
    else:
        print(f"Logo file not found: {logo_png_source_file}. Skipping logo generation.")

    # --- Generate sprites.h (for walking sprites ONLY) ---
    walking_sprites_h_aggregator_path = os.path.join(walking_sprites_output_dir, "sprites.h")
    
    with open(walking_sprites_h_aggregator_path, 'w') as f:
        f.write("""/** @file sprites.h @brief Includes headers for WALKING animation sprites ONLY. */
#ifndef LVGL_WALKING_SPRITES_H
#define LVGL_WALKING_SPRITES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
""") # This block is a raw string, no f-string needed.
        if walking_sprite_details:
            f.write("\n// Include walking sprite headers\n")
            for _, header_filename in walking_sprite_details:
                f.write(f'#include "{header_filename}"\n') # Simple f-string for include line
        else:
            f.write("\n// No walking sprites found or processed.\n")
        
        f.write("""
// Array of all walking animation sprites
extern const lv_img_dsc_t* walking_sprites[];
extern const uint8_t walking_sprites_count;

#ifdef __cplusplus
}
#endif

#endif /* LVGL_WALKING_SPRITES_H */
""") # This block is also a raw string.
    print(f"Generated walking sprites aggregator: {walking_sprites_h_aggregator_path}")

    # --- Generate sprites.c (for walking sprites ONLY) ---
    walking_sprites_c_aggregator_path = os.path.join(walking_sprites_output_dir, "sprites.c")
    with open(walking_sprites_c_aggregator_path, 'w') as f:
        f.write("""/** @file sprites.c @brief Defines the walking_sprites array (WALKING animation ONLY). */
#include "sprites.h"

""") # Raw string block.
        if walking_sprite_details:
            f.write("""// Define the walking_sprites array
const lv_img_dsc_t* walking_sprites[] = {
""") # Raw string block.
            for var_name, _ in walking_sprite_details:
                f.write(f"    &sprite_{var_name},\n") # Simple f-string
            f.write("};\n\n") # Raw string
            f.write(f"const uint8_t walking_sprites_count = {len(walking_sprite_details)};\n") # Simple f-string
        else:
            f.write("""// No walking sprites processed, array is empty.
const lv_img_dsc_t* walking_sprites[] = { NULL };
const uint8_t walking_sprites_count = 0;
""") # Raw string block.
    print(f"Generated walking sprites C source: {walking_sprites_c_aggregator_path}")
    
    total_sprites = len(walking_sprite_details) + (1 if logo_var_name else 0)
    print(f"Successfully processed {total_sprites} sprites in total.")
    if logo_var_name:
        print(f"  - Logo sprite '{logo_var_name}' generated in {logo_sprite_output_dir}")
    print(f"  - {len(walking_sprite_details)} walking sprites generated in {walking_sprites_output_dir}")

if __name__ == '__main__':
    main() 