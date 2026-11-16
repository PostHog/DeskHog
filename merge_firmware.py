"""Make one file that holds everything a board needs: deskhog-full.bin.

A normal upload writes four pieces to four places: the bootloader, the
partition table, the "boot the first slot" data and the firmware itself.
Someone installing from a browser should not have to know that. This script
joins the same pieces, at the same offsets the upload uses, into one image
that is written at offset 0.

It runs after every build and leaves the result next to firmware.bin:

    .pio/build/<env>/deskhog-full.bin    a board that has not run this firmware yet (write at 0x0)
    .pio/build/<env>/firmware.bin        an update for a board that has (write at the app offset, 0x20000)

Writing the full image also blanks the settings area, which lies between the
pieces, so WiFi and the list of cards are forgotten. The update file leaves
them alone.
"""

Import("env")  # noqa: F821  (provided by PlatformIO)


def merge(source, target, env):
    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [str(offset), env.subst(str(path))]
    images += [env.subst("$ESP32_APP_OFFSET"), str(target[0])]
    output = env.subst("$BUILD_DIR/deskhog-full.bin")
    command = [
        '"$PYTHONEXE"', '"$OBJCOPY"', "--chip", env.BoardConfig().get("build.mcu", "esp32s3"),
        "merge_bin", "-o", '"%s"' % output,
    ] + ['"%s"' % part for part in images]
    result = env.Execute(" ".join(command))
    if result:
        print("merge_firmware: could not make %s" % output)
    else:
        print("merge_firmware: %s holds %s" % (output, " ".join(images)))
    return result


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
