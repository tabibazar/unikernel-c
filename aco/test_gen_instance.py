import subprocess, sys, pathlib
HERE = pathlib.Path(__file__).parent

def gen(name, optimum="0"):
    out = subprocess.run(
        [sys.executable, str(HERE / "gen_instance.py"),
         str(HERE / "fixtures" / f"{name}.tsp"), optimum],
        capture_output=True, text=True, check=True)
    return out.stdout

def test_dimension_and_coords():
    h = gen("tiny5")
    assert "#define ACO_N 5" in h
    assert '#define ACO_NAME "tiny5"' in h
    assert "{0, 0}" in h and "{6, 8}" in h

def test_optimum_is_emitted():
    assert "#define ACO_OPTIMUM 1234" in gen("tiny5", "1234")

def test_rejects_non_euc2d(tmp_path):
    bad = tmp_path / "bad.tsp"
    bad.write_text("NAME: bad\nDIMENSION: 2\nEDGE_WEIGHT_TYPE: ATT\n"
                   "NODE_COORD_SECTION\n1 0 0\n2 1 1\nEOF\n")
    r = subprocess.run([sys.executable, str(HERE / "gen_instance.py"), str(bad), "0"],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "EUC_2D" in r.stderr
