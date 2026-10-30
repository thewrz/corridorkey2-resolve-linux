import pytest

from scripts.desktop import with_plugin_path


def test_install_preserves_gpu_options_and_is_idempotent():
    original = "[Desktop Entry]\nExec=env GPU_SETTING=1 /opt/resolve/bin/resolve %u\nIcon=resolve\n"
    result = with_plugin_path(original, "/home/test user/OFX/Plugins")
    assert 'OFX_PLUGIN_PATH="/home/test user/OFX/Plugins"' in result
    assert "env GPU_SETTING=1 /opt/resolve/bin/resolve %u" in result
    assert with_plugin_path(result, "/home/test user/OFX/Plugins") == result


def test_install_never_discards_existing_plugin_search_path():
    with pytest.raises(ValueError, match="already sets"):
        with_plugin_path("Exec=env OFX_PLUGIN_PATH=/other resolve", "/plugins")


def test_install_rejects_desktop_argument_injection():
    with pytest.raises(ValueError):
        with_plugin_path("Exec=resolve", '/tmp/path" --unwanted')
