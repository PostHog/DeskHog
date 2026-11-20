"""Flash the updater along with the main firmware.

The main firmware cannot be updated over WiFi without the updater in its own
slot (see partitions.csv), so an upload over USB and deskhog-full.bin both carry
it. This script builds the updater when it is missing or older than its source,
and adds it to the images PlatformIO flashes. merge_firmware.py runs after it
and picks it up from the same list.
"""

import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)

project = env.subst("$PROJECT_DIR")  # noqa: F821
binary = os.path.join(project, ".pio", "build", "updater", "firmware.bin")
sources = [os.path.join(project, "updater", "main.cpp"), os.path.join(project, "partitions.csv"),
           os.path.join(project, "platformio.ini")]


def factory_offset():
    with open(env.subst("$PARTITIONS_TABLE_CSV")) as handle:  # noqa: F821
        for line in handle:
            columns = [column.strip() for column in line.strip().split(",")]
            if len(columns) >= 5 and columns[1] == "app" and columns[2] == "factory":
                return columns[3]
    return None


def stale():
    if not os.path.exists(binary):
        return True
    built = os.path.getmtime(binary)
    return any(os.path.getmtime(source) > built for source in sources)


offset = factory_offset()
if offset is None:
    print("include_updater: partitions.csv has no slot for the updater; it is left out")
else:
    if stale():
        print("include_updater: building the updater")
        result = subprocess.call([env.subst("$PYTHONEXE"), "-m", "platformio", "run", "-e", "updater", "-d", project, "-s"])  # noqa: F821
        if result != 0 or not os.path.exists(binary):
            print("include_updater: the updater did not build")
            env.Exit(1)  # noqa: F821
        os.utime(binary, None)
    env.Append(FLASH_EXTRA_IMAGES=[(offset, binary)])  # noqa: F821
    env.Append(UPLOADERFLAGS=[offset, binary])  # noqa: F821
    print("include_updater: the updater (%d bytes) goes to %s" % (os.path.getsize(binary), offset))
