"""CPU/mock regressions. These do not execute libobs, HLSL, D3D11, or NVENC."""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^[\w *]+\b" + name + r"\([^;]*?\)(?:\s*:\s*\w+)?\s*\{", source, re.M)
    if not match:
        raise AssertionError(f"Missing function: {name}")
    start = match.end() - 1
    depth = 0
    for pos in range(start, len(source)):
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[match.start():pos + 1]
    raise AssertionError(f"Unbalanced function: {name}")


def decode(x):
    return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4


def half(x):
    return struct.unpack("e", struct.pack("e", x))[0]


def warp(x, y, width=1920, height=1080, expansion=100, px=.76, py=.80):
    # Inverse map: output pixel -> input UV. Includes the plugin template transform.
    u, v = (x + .5 - expansion) / width, (y + .5 - expansion) / height
    dx, dy = u - .5, v - .5
    r, b = math.hypot(dx, dy), math.sqrt(.5)
    if r == 0:
        return u, v
    return (.5 + dx / r * math.tan(r * px) * b / math.tan(b * px),
            .5 + dy / r * math.tan(r * py) * b / math.tan(b * py))


class NumericalTests(unittest.TestCase):
    def test_measurement_planar_layout_and_pq(self):
        spec = importlib.util.spec_from_file_location('hdr_measure', ROOT / 'tools/measure-hdr.py')
        measure = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(measure)
        frame = [512]*16 + [123]*4 + [234]*4
        self.assertEqual(measure.sample(frame, 3, 3, 4, 4), (512, 123, 234))
        self.assertEqual(measure.pq_nits(64), 0)
        self.assertAlmostEqual(measure.pq_nits(940), 10000, places=7)

    def test_measurement_cli_identity(self):
        with tempfile.TemporaryDirectory(prefix='hdr-measure-') as directory:
            path = Path(directory)
            frame = path / 'frame.yuv'
            frame.write_bytes(struct.pack('<96H', *([512]*64 + [512]*32)))
            patches = path / 'patches.json'
            patches.write_text('[{"name":"neutral", "rect":[0,0,8,8], "neutral":true}]')
            result = subprocess.run([sys.executable, str(ROOT/'tools/measure-hdr.py'),
                                     '--clean', str(frame), '--test', str(frame), '--patches', str(patches),
                                     '--width','8','--height','8','--source-width','8','--source-height','8',
                                     '--clean-origin','0','0','--test-origin','0','0','--margin','0'],
                                    capture_output=True, text=True, check=True)
            self.assertTrue(json.loads(result.stdout)['passed'])

    def test_failure_signature(self):
        measured = [.8, 27, 125, 301, 1479, 4965]
        predicted = [300 * decode(n / 300) for n in [10, 100, 203, 300, 600, 1000]]
        for actual, expected in zip(predicted, measured):
            self.assertLess(abs(actual - expected), max(1.1, expected * .01))

    def test_half_float_identity_and_repetition(self):
        # Two raw float16 intermediate stores per application. Includes signed wide gamut.
        for value in [-.25, -.001, 0, .001, 10/300, 100/300, 1, 2, 1000/300, 16]:
            result = value
            for _ in range(100):
                result = half(half(result))
            self.assertLessEqual(abs(result - value), max(abs(value) * .00049, 3e-8))
            self.assertEqual(result, half(value))

    def test_output_effect_transfer_guard(self):
        effect = (ROOT / "data/internal/render_output.effect").read_text()
        self.assertRegex(effect, r"uniform bool convert_linear\s*=\s*true")
        self.assertRegex(effect, r"if\s*\(convert_linear\)\s*px.xyz\s*=\s*srgb_nonlinear_to_linear")
        self.assertNotRegex(effect, r"saturate\s*\(|clamp\s*\(")

    def test_identity_shader_has_no_color_operation(self):
        identity = (ROOT / "data/examples/filter_template.shader").read_text()
        body = function(identity, "mainImage")
        self.assertRegex(body, r"return image.Sample\(textureSampler, v_in.uv\);")
        self.assertNotRegex(body, r"pow\(|saturate\(|\.rgb\s*=")

    def test_fisheye_center_and_sample_only_color(self):
        source = (ROOT / "data/examples/fisheye-xy.shader").read_text()
        self.assertIn('#define USE_PM_ALPHA 1', source)
        self.assertIn("if (distance(center_pos, v_in.uv) == 0.0)", source)
        self.assertNotRegex(source, r"\.rgb|\.xyz|pow\(|saturate\(|clamp\(")
        self.assertIn("return image.Sample(textureSampler, uv);", source)
        self.assertEqual(warp(1059.5, 639.5), (.5, .5))

    def test_fisheye_expansion_symmetry_and_correspondence(self):
        # Coordinate tests; actual texture filtering still requires a GPU test.
        for x, y in [(200, 200), (500, 300), (1060, 640), (1800, 1000)]:
            u, v = warp(x, y)
            a, b = warp(2119-x, 1279-y)
            self.assertAlmostEqual(u+a, 1)
            self.assertAlmostEqual(v+b, 1)
            self.assertTrue(math.isfinite(u) and math.isfinite(v))
        self.assertLess(warp(0, 0)[0], 0)  # transparent border, not an overlay
        self.assertNotAlmostEqual(warp(400, 200)[0], (400+.5-100)/1920)


def run_c_tests(compiler):
    source = (ROOT / "obs-shaderfilter.c").read_text()
    names = ["create_or_reset_texrender", "reset_render_color_space", "get_input_source",
             "draw_output", "shader_filter_get_color_space", "render_shader", "shader_filter_render"]
    bodies = "\n\n".join(function(source, name) for name in names)
    support = (ROOT / "tests/render_mock.c").read_text()
    with tempfile.TemporaryDirectory(prefix="shaderfilter-tests-") as directory:
        path = Path(directory)
        generated = path / "render_test.c"
        generated.write_text(support.replace("/* ACTUAL_PLUGIN_FUNCTIONS */", bodies))
        executable = path / "render_test.exe"
        subprocess.run([*compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(generated), "-o", str(executable),
                        *(["-lm"] if os.name != "nt" else [])], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help='C compiler command, e.g. "zig cc" or "clang"')
    args = parser.parse_args()
    run_c_tests(shlex.split(args.cc, posix=True))
    unittest.main(argv=[__file__], verbosity=2)
