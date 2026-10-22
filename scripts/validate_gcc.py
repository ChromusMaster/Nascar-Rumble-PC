import argparse
import os
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--output", required=True)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
cmake = (root / "CMakeLists.txt").read_text()
core = re.search(r"set\(NR_CORE_SOURCES(.*?)\)", cmake, re.S).group(1).split()
ui = ["src/launcher/ltext.c", "src/launcher/lfont.c", "src/launcher/l10n.c", "src/ui/font8x8.c"]
targets = {
    "nr_atomic_io": ["tests/test_atomic_io.c"],
    "nrtests": ["tests/test_main.c", *ui],
    "nrengine_headless": ["src/app/main_headless.c"],
    "nrtool": ["src/tools/nrtool.c"],
    "nr_config_security": ["tests/test_config_security.c"],
    "nr_font_security": ["tests/test_font_security.c", "src/launcher/lfont.c"],
    "nr_render_security": ["tests/test_render_security.c"],
    "nr_mod_security": ["tests/test_mod_security.c"],
}
flags = ["-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-misleading-indentation", "-Isrc", '-DNR_VERSION_STRING="0.3.2"', f'-DNR_GOLDEN_FILE="{root / "tests/golden_slus01068.txt"}"']
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
objects = {}
with (out / "build.log").open("w") as log:
    def run(command):
        print(" ".join(map(str, command)), file=log, flush=True)
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    for source in dict.fromkeys(core + [s for sources in targets.values() for s in sources]):
        obj = out / (source.replace("/", "_") + ".o")
        run(["gcc", *flags, "-c", source, "-o", str(obj)])
        objects[source] = str(obj)
    archive = out / "libnrcore.a"
    run(["ar", "rcs", str(archive), *(objects[s] for s in core)])
    for name, sources in targets.items():
        wrap = ["-Wl,--wrap=" + symbol for symbol in ("fwrite", "fflush", "fsync", "fclose", "rename")] if name == "nr_atomic_io" else []
        run(["gcc", *flags, *wrap, *(objects[s] for s in sources), str(archive), "-lm", "-ldl", "-pthread", "-o", str(out / name)])
failed = False
for name in ("nrtests", "nr_config_security", "nr_font_security", "nr_render_security", "nr_mod_security", "nr_atomic_io"):
    with (out / (name + ".log")).open("w") as log:
        result = subprocess.run([str(out / name)], cwd=out, env=os.environ.copy(), stdout=log, stderr=subprocess.STDOUT)
    print(name, result.returncode, flush=True)
    failed |= result.returncode != 0
raise SystemExit(int(failed))
