from __future__ import annotations
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from Cython.Build import cythonize
import os, glob, shutil, sys


def _repo_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))


ROOT = _repo_root()


def _discover_include_dirs(root: str) -> list[str]:
    """Return include directories for public and generated headers.

    Prefers real repo headers, falls back to vendored copy inside package.
    Also appends generated include directories if present.
    """
    include_dirs: list[str] = []
    repo_include = os.path.join(root, 'include')
    vendored_include = os.path.join(os.path.dirname(__file__), 'astl', 'include')
    if os.path.isdir(repo_include):
        include_dirs.append(repo_include)
    elif os.path.isdir(vendored_include):
        include_dirs.append(vendored_include)
    else:
        # Harmless: include path to aid diagnostics on failures
        include_dirs.append(repo_include)

    for gen_dir in [
        os.path.join(root, 'build', 'debug', 'include'),
        os.path.join(root, 'build', 'release', 'include'),
    ]:
        if os.path.isdir(gen_dir):
            include_dirs.append(gen_dir)
    return include_dirs


def _find_built_library(root: str) -> tuple[str | None, list[str], list[str]]:
    """Discover a prebuilt libastl-*.so and return (lib_name, lib_dirs, runtime_rpaths)."""
    lib_dirs: list[str] = []
    candidate_builds = [
        os.path.join(root, 'build', 'debug', 'lib'),
        os.path.join(root, 'build', 'release', 'lib'),
    ]
    lib_name: str | None = None
    for d in candidate_builds:
        if not os.path.isdir(d):
            continue
        matches = glob.glob(os.path.join(d, 'libastl-*.so'))
        if matches:
            lib_dirs.append(d)
            base = os.path.basename(matches[0])  # libastl-<MAJOR>.so
            lib_name = base[len('lib') : -len('.so')]
            break
    rpaths: list[str] = []
    if sys.platform.startswith("linux") and lib_name:
        rpaths = ["$ORIGIN"]
    return lib_name, lib_dirs, rpaths


def _make_extensions(include_dirs: list[str], lib_name: str | None, lib_dirs: list[str], rpaths: list[str]) -> list[Extension]:
    libraries = [lib_name] if lib_name else []
    exts = [
        Extension(
            "astl._core",
            sources=["astl/_core.pyx"],
            include_dirs=include_dirs,
            library_dirs=lib_dirs,
            libraries=libraries,
            language="c++",
            extra_compile_args=["-std=c++17"],
            runtime_library_dirs=rpaths or None,
        )
    ]
    # Optional sanitizer flags
    if os.environ.get("ASTL_PYTHON_ENABLE_ASAN"):
        san_flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for ext in exts:
            if san_flags[0] not in ext.extra_compile_args:
                ext.extra_compile_args.extend(san_flags)
            ext.extra_link_args = getattr(ext, 'extra_link_args', []) + ["-fsanitize=address,undefined"]
    return exts


class build_ext_with_copy(build_ext):
    """Custom build_ext: after building, copy LICENSE and bundle libastl-*.so.

    Simplified single run() implementation to keep cognitive complexity low.
    """

    def run(self):  # type: ignore[override]
        super().run()

        pkg_dir = os.path.join(os.path.dirname(__file__), "astl")
        os.makedirs(pkg_dir, exist_ok=True)

        # Copy LICENSE (best effort)
        top_license = os.path.join(ROOT, "LICENSE")
        if os.path.isfile(top_license):
            try:
                shutil.copy2(top_license, os.path.join(pkg_dir, "LICENSE"))
            except OSError:
                pass

        if not LIB_NAME:
            return

        # Remove stale bundled libs
        for old in glob.glob(os.path.join(pkg_dir, "libastl-*.so")):
            try:
                os.remove(old)
            except OSError:
                pass

        # Find built shared library
        lib_src = None
        for d in LIB_DIRS:
            try:
                matches = glob.glob(os.path.join(d, f"lib{LIB_NAME}.so"))
                if matches:
                    lib_src = matches[0]
                    break
            except OSError:
                continue
        if not lib_src:
            return

        def _copy_into(dst_dir: str):
            try:
                os.makedirs(dst_dir, exist_ok=True)
                shutil.copy2(lib_src, os.path.join(dst_dir, os.path.basename(lib_src)))
            except OSError:
                pass

        # Copy next to built extension
        for ext in self.extensions:
            if ext.name == "astl._core":
                ext_path = self.get_ext_fullpath(ext.name)
                _copy_into(os.path.dirname(ext_path))
                break
        # And into source package dir
        _copy_into(pkg_dir)

INCLUDE_DIRS = _discover_include_dirs(ROOT)
LIB_NAME, LIB_DIRS, RPATHS = _find_built_library(ROOT)
EXTENSIONS = _make_extensions(INCLUDE_DIRS, LIB_NAME, LIB_DIRS, RPATHS)

package_data = {"astl": []}
if LIB_NAME:
    package_data["astl"].append(f"lib{LIB_NAME}.so")

_pkg_license_path = os.path.join(os.path.dirname(__file__), 'astl', 'LICENSE')
if os.path.isfile(_pkg_license_path):
    package_data["astl"].append("LICENSE")

def read_version():
    # Prefer top-level VERSION.md (first non-empty line). Fallback to 0.0.0 if absent.
    ver_file = os.path.join(ROOT, 'VERSION.md')
    try:
        with open(ver_file, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    return line
    except OSError:
        pass
    return '0.0.0'

setup(
    name="astl",
    version=read_version(),
    ext_modules=cythonize(EXTENSIONS, language_level="3", compiler_directives={"binding": False}),
    packages=["astl"],
    package_data=package_data,
    cmdclass={"build_ext": build_ext_with_copy},
    zip_safe=False,
)

print(f"[astl setup] Detected shared library name: {LIB_NAME!r} (dirs searched: {[os.path.join(ROOT, 'build', 'debug', 'lib'), os.path.join(ROOT, 'build', 'release', 'lib')]})", file=sys.stderr)
if not LIB_NAME:
    print("[astl setup] WARNING: No prebuilt libastl-*.so discovered; extension will link at runtime (LD_LIBRARY_PATH may be required).", file=sys.stderr)
