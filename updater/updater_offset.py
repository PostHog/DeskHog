"""Upload the updater to its own slot.

PlatformIO flashes a firmware where the partition table has `ota_0`, which is
the main firmware's slot. The updater lives in the `factory` slot of the same
table, so this script points the upload there.
"""

Import("env")  # noqa: F821  (provided by PlatformIO)


def factory_offset():
    with open(env.subst("$PARTITIONS_TABLE_CSV")) as handle:  # noqa: F821
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            columns = [column.strip() for column in line.split(",")]
            if len(columns) >= 5 and columns[1] == "app" and columns[2] == "factory":
                return columns[3]
    return None


offset = factory_offset()
if offset is None:
    print("updater_offset: partitions.csv has no factory slot for the updater")
    env.Exit(1)  # noqa: F821
env.Replace(ESP32_APP_OFFSET=offset)  # noqa: F821
print("updater_offset: the updater goes to %s" % offset)
