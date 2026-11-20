"""Copy the files of one LittleFS image into a new image of another size.

Used by `tools/deskhog migrate`, which moves a device's plugin storage to a
smaller partition. Run with PlatformIO's Python, which has the `littlefs`
package:

    ~/.platformio/penv/bin/python tools/repack_storage.py old.bin new.bin <new size in bytes>

It prints how many files it copied, and exits with an error (and writes no
image) when the old image cannot be read, holds no files, or does not fit.
Every file is read back from the new image and compared before it is saved.
"""

import sys

from littlefs import LittleFS

BLOCK = 4096  # what the firmware's LittleFS uses


def mounted(data):
    fs = LittleFS(block_size=BLOCK, block_count=len(data) // BLOCK, mount=False, name_max=255)
    fs.context.buffer = bytearray(data)
    fs.mount()
    return fs


def files_of(fs):
    folders, files = [], {}
    for root, dirs, names in fs.walk("/"):
        base = root.rstrip("/")
        folders += [base + "/" + name for name in dirs]
        for name in names:
            with fs.open(base + "/" + name, "rb") as handle:
                files[base + "/" + name] = handle.read()
    return folders, files


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    source, target, size = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    with open(source, "rb") as handle:
        old = handle.read()
    try:
        folders, files = files_of(mounted(old))
    except Exception as error:  # the library raises its own error types
        sys.exit("could not read the plugin storage in %s: %s" % (source, error))
    if not files:
        sys.exit("the plugin storage in %s holds no files" % source)

    fs = LittleFS(block_size=BLOCK, block_count=size // BLOCK, name_max=255)
    try:
        for folder in sorted(folders):
            fs.makedirs(folder, exist_ok=True)
        for path, data in sorted(files.items()):
            with fs.open(path, "wb") as handle:
                handle.write(data)
    except Exception as error:
        sys.exit("the %d files (%d KB) do not fit in %d KB: %s"
                 % (len(files), sum(map(len, files.values())) // 1024, size // 1024, error))
    image = bytes(fs.context.buffer)

    again_folders, again = files_of(mounted(image))
    if again != files or sorted(again_folders) != sorted(folders):
        sys.exit("the new image does not hold the same files as the old one")
    with open(target, "wb") as handle:
        handle.write(image)
    print("%d files, %d KB" % (len(files), sum(map(len, files.values())) // 1024))


if __name__ == "__main__":
    main()
