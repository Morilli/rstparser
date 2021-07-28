# rstparser
RST / Riot stringtable / .txt because they didn't rename them / .stringtable parser for league of legends.

Should be able to just drag-and-drop an RST file on `rstparser` and it'll generate a new `[original/file/name].txt` file (it just appends `.txt` to the original name).

The generated file will look similar to the files pre-binary-format. Because it's now a binary format, a hashfile is needed to convert from hashes to readable key names.

Note that currently the `hashes.rst.txt` file must be near the executable for it to get loaded.
The provided hashtable is not complete, so a lot of keys will show up as numbers instead.
