#!/usr/bin/env python3
import os
import sys
import glob
import re
import subprocess
import json

def check_npm_installed():
    """Check if npm is installed"""
    try:
        subprocess.check_call(["npm", "--version"], 
                             stdout=subprocess.DEVNULL, 
                             stderr=subprocess.DEVNULL)
        return True
    except (subprocess.SubprocessError, FileNotFoundError):
        return False

def convert_font(ttf_file, output_dir, size, font_name_override=None):
    """Convert TTF file to LVGL compatible C font"""
    font_name = font_name_override if font_name_override else safe_font_name(ttf_file)
    output_c_file = os.path.join(output_dir, f"{font_name}.c")
    
    # Define character range (0-255, basic Latin)
    # Use a simplified range for LoudNoises font which may have limited glyphs
    if "LoudNoises" in ttf_file or font_name == "font_loud_noises":
        range_arg = "0x20-0x7F"  # Basic Latin only
    else:
        range_arg = "0x20-0x7F,0xA0-0xFF"  # Full Latin
    
    # Build the lv_font_conv command with appropriate parameters
    cmd = [
        "npx", "lv_font_conv",
        "--font", ttf_file,
        "--range", range_arg,
        "--size", str(size),  # Use the provided size
        "--format", "lvgl",
        "--bpp", "4",  # 4 bits per pixel for grayscale
        "--no-compress",
        "--output", output_c_file,
        "--lv-font-name", font_name  # Correct param for LVGL font name
    ]
    
    try:
        print(f"Converting {ttf_file} at {size}pt as {font_name}...")
        subprocess.check_call(cmd)
        
        if os.path.exists(output_c_file):
            # Fix the include path in the generated file
            with open(output_c_file, 'r') as f:
                content = f.read()
            
            # Replace the include path
            content = content.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
            
            with open(output_c_file, 'w') as f:
                f.write(content)
            
            # Also create a header file for inclusion
            header_file = os.path.join(output_dir, f"{font_name}.h")
            with open(header_file, 'w') as f:
                header_content = f"""/**
 * @file {font_name}.h
 * @brief LVGL font generated from {os.path.basename(ttf_file)} at {size}pt
 */

#pragma once

#ifdef __cplusplus
extern "C" {{
#endif

#include "lvgl.h"

extern const lv_font_t {font_name};

#ifdef __cplusplus
}}
#endif
"""
                f.write(header_content)
            print(f"Generated {output_c_file} and {header_file}")
            return True
        else:
            print(f"Failed to generate {output_c_file}")
            return False
    except subprocess.SubprocessError as e:
        print(f"Error converting font: {e}")
        return False

def safe_font_name(filename):
    """Convert filename to safe C variable name"""
    # Remove file extension and path
    name = os.path.splitext(os.path.basename(filename))[0]
    # Replace non-alphanumeric with underscore
    name = re.sub(r'[^a-zA-Z0-9]', '_', name)
    return name

def main():
    print("TTF to LVGL Font Converter")
    print("==========================")
    
    # Check if npm is installed
    if not check_npm_installed():
        print("npm is required but not installed. Please install Node.js and npm first.")
        sys.exit(1)
    
    # Install lv_font_conv if not already installed
    print("Installing lv_font_conv (if not already installed)...")
    try:
        subprocess.check_call(["npm", "install", "--no-save", "lv_font_conv"])
    except subprocess.SubprocessError as e:
        print(f"Failed to install lv_font_conv: {e}")
        print("Please install manually with: npm install -g lv_font_conv")
        sys.exit(1)
    
    # Create output directory if it doesn't exist
    output_dir = "include/fonts"
    os.makedirs(output_dir, exist_ok=True)
    
    # Define the fonts to create
    font_configs = [
        {"name": "font_label", "file": "typography/Inter_18pt-Regular.ttf", "size": 15},
        {"name": "font_value", "file": "typography/Inter_18pt-SemiBold.ttf", "size": 16},
        {"name": "font_value_large", "file": "typography/Inter_18pt-SemiBold.ttf", "size": 36},
        {"name": "font_loud_noises", "file": "typography/LoudNoises.ttf", "size": 20},
        # For plugins: gfx.text(..., "tiny"), for dense lists
        {"name": "font_tiny", "file": "typography/Inter_18pt-Regular.ttf", "size": 10}
    ]
    
    success_count = 0
    converted_fonts = []
    
    for config in font_configs:
        if os.path.exists(config["file"]):
            if convert_font(config["file"], output_dir, config["size"], config["name"]):
                success_count += 1
                converted_fonts.append(config["name"])
        else:
            print(f"Font file not found: {config['file']}")
    
    # Generate fonts.h file that includes all font headers
    fonts_h_content = """/**
 * @file fonts.h
 * @brief Includes all LVGL font headers
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Include all font headers
"""
    for font_name in converted_fonts:
        fonts_h_content += f'#include "{font_name}.h"\n'
    
    fonts_h_content += """
#ifdef __cplusplus
}
#endif
"""
    
    with open(os.path.join(output_dir, "fonts.h"), 'w') as f:
        f.write(fonts_h_content)
    
    print(f"Generated {output_dir}/fonts.h")
    print(f"Successfully processed {success_count} of {len(font_configs)} fonts")
    
    if success_count == len(font_configs):
        print("All fonts were successfully converted to LVGL format!")
    else:
        print("Some fonts could not be processed. See errors above.")
    
    print("\nNext steps:")
    print("1. Include 'fonts.h' in your code")
    print("2. Use the fonts with LVGL like this:")
    print("   - For labels: lv_style_set_text_font(&style, &font_label);")
    print("   - For values: lv_style_set_text_font(&style, &font_value);")
    print("   - For large values: lv_style_set_text_font(&style, &font_value_large);")

if __name__ == "__main__":
    main() 