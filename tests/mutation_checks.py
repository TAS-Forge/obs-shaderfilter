"""Prove that six specific rendering regressions fail the compiled C harness."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile
import run_tests

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = {
    'stale format reuse': (
        '\tif (render && gs_texrender_get_format(render) != format) {\n\t\tgs_texrender_destroy(render);\n\t\trender = NULL;\n\t}', ''),
    'input target defaults to SDR': ('\t\t\t\t\t     source_space)) {', '\t\t\t\t\t     GS_CS_SRGB)) {'),
    'output target defaults to SDR': ('filter->total_height, space)) {', 'filter->total_height, GS_CS_SRGB)) {'),
    'inherited framebuffer encode': ('gs_enable_framebuffer_srgb(false);', 'gs_enable_framebuffer_srgb(previous_srgb);'),
    'unconditional output decode': ('source_space == GS_CS_SRGB);', 'true);'),
    'reuse history across float spaces': ('filter->render_space == space)',
        'gs_get_format_from_space(filter->render_space) == gs_get_format_from_space(space))'),
}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    original = (ROOT/'obs-shaderfilter.c').read_text()
    for name, (old, new) in MUTATIONS.items():
        assert original.count(old) == 1, (name, 'mutation target changed')
        with tempfile.TemporaryDirectory(prefix='hdr-mutation-') as directory:
            path = Path(directory)
            (path/'tests').mkdir()
            (path/'tests/render_mock.c').write_text((ROOT/'tests/render_mock.c').read_text())
            (path/'obs-shaderfilter.c').write_text(original.replace(old,new))
            run_tests.ROOT = path
            try:
                run_tests.run_c_tests(shlex.split(args.cc))
            except subprocess.CalledProcessError as error:
                if Path(error.cmd[0]).name != 'render_test.exe':
                    raise  # A compile failure does not prove the runtime assertion detects the bug.
                print(f'PASS: rejected {name}', flush=True)
            else:
                raise AssertionError(f'Regression survived: {name}')
