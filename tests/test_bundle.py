import ctypes
from pathlib import Path


def test_native_bundle_exports_one_image_effect():
    path = Path("build/CorridorKey2.ofx.bundle/Contents/Linux-x86-64/CorridorKey2.ofx")
    assert path.is_file(), "Native OpenFX bundle must be built"
    library = ctypes.CDLL(str(path.resolve()))
    assert library.OfxGetNumberOfPlugins() == 1

    class Plugin(ctypes.Structure):
        _fields_ = [("api", ctypes.c_char_p), ("api_version", ctypes.c_int),
                    ("identifier", ctypes.c_char_p), ("major", ctypes.c_uint),
                    ("minor", ctypes.c_uint), ("set_host", ctypes.c_void_p),
                    ("entry", ctypes.c_void_p)]

    library.OfxGetPlugin.restype = ctypes.POINTER(Plugin)
    plugin = library.OfxGetPlugin(0).contents
    assert plugin.api == b"OfxImageEffectPluginAPI"
    assert plugin.identifier == b"org.corridorkey.community.resolve.v2"
    assert plugin.entry and plugin.set_host


def test_bundle_contains_current_worker_and_required_notices():
    resources = Path("build/CorridorKey2.ofx.bundle/Contents/Resources")
    for source in Path("worker").glob("*.py"):
        assert (resources / "worker" / source.name).read_bytes() == source.read_bytes()
    assert (resources / "THIRD_PARTY_NOTICES.md").is_file()
    for name in ("OpenFX-Support.txt", "nlohmann-json-MIT.txt",
                 "GPL-3.0.txt", "GCC-Runtime-Exception.txt"):
        assert (resources / "licenses" / name).is_file()
