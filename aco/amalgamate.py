#!/usr/bin/env python3
"""Inline the instance header into aco.c to make one self-contained file.

BareMetal-App's 1-build.sh compiles exactly one .c file, copied into its own
directory, so a relative #include of instances/<name>.h does not resolve there.
Splicing the header in removes the include entirely rather than fighting the
include path.
"""
import sys, pathlib, re

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: amalgamate.py <instance-name> <out.c>")
    name, out = sys.argv[1], sys.argv[2]
    here = pathlib.Path(__file__).parent
    src = (here / "aco.c").read_text()
    hdr = (here / "instances" / f"{name}.h").read_text()
    # Drop the header's include guards; it is spliced exactly once.
    hdr = re.sub(r"^#ifndef ACO_INSTANCE_\w+_H\n#define ACO_INSTANCE_\w+_H\n",
                 "", hdr, flags=re.M)
    hdr = re.sub(r"\n#endif\s*$", "\n", hdr)
    spliced = src.replace("#include ACO_INSTANCE_HEADER",
                          "/* --- instance spliced in by amalgamate.py --- */\n" + hdr)
    if spliced == src:
        sys.exit("error: could not find '#include ACO_INSTANCE_HEADER' in aco.c")
    if "ACO_INSTANCE_HEADER" in spliced.replace("#ifndef ACO_INSTANCE_HEADER", "")\
                                       .replace("#define ACO_INSTANCE_HEADER", ""):
        pass  # the guard block is harmless; the include itself is gone
    pathlib.Path(out).write_text(spliced)
    print(f"{out}: {len(spliced.splitlines())} lines, instance {name}")

if __name__ == "__main__":
    main()
