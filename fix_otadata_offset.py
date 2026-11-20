"""Flash the boot-selection data where this project's partition table puts it.

The Arduino framework always flashes boot_app0.bin (8 KB, "boot the first app
slot") at 0xe000, where its default partition table has the otadata partition.
This project's table has otadata at 0xf000, and 0xe000 is the last page of
nvs: the settings. So every USB upload overwrote the newest settings, which
showed up as cards (and eventually WiFi) vanishing after a firmware update.

This script moves that one image to the otadata offset from partitions.csv.
It runs after PlatformIO has built the upload command, so it patches both
the list of images and the command.
"""

import os

Import("env")  # noqa: F821  (provided by PlatformIO)


def otadata_offset():
    path = env.subst("$PARTITIONS_TABLE_CSV")  # noqa: F821
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            columns = [column.strip() for column in line.split(",")]
            if len(columns) >= 5 and columns[1] == "data" and columns[2] == "ota":
                return columns[3]
    return None


def is_boot_app0(path):
    return os.path.basename(str(path)) == "boot_app0.bin"


offset = otadata_offset()
if offset is None:
    # No otadata partition: there is nothing to initialise, so do not flash it at all.
    print("fix_otadata_offset: no otadata partition; boot_app0.bin will not be flashed")

images = []
for image_offset, image_path in env.get("FLASH_EXTRA_IMAGES", []):  # noqa: F821
    if is_boot_app0(image_path):
        if offset is None:
            continue
        image_offset = offset
    images.append((image_offset, image_path))
env.Replace(FLASH_EXTRA_IMAGES=images)  # noqa: F821

flags = list(env.get("UPLOADERFLAGS", []))  # noqa: F821
patched = []
index = 0
while index < len(flags):
    following = flags[index + 1] if index + 1 < len(flags) else None
    if following is not None and is_boot_app0(following):
        if offset is not None:
            patched.extend([offset, following])
        index += 2
        continue
    patched.append(flags[index])
    index += 1
env.Replace(UPLOADERFLAGS=patched)  # noqa: F821
if offset is not None:
    print("fix_otadata_offset: boot_app0.bin goes to %s (otadata), not 0xe000 (nvs)" % offset)
