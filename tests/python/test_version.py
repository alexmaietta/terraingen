import re

import terraingen as tg


def test_version_matches_cmake(repo_root):
    cmake = (repo_root / "CMakeLists.txt").read_text()
    expected = re.search(r'set\(TERRAINGEN_FULL_VERSION "([^"]+)"\)', cmake).group(1)
    assert tg.__version__ == expected
    assert ".".join(str(part) for part in tg.version()) == expected


def test_api_version_is_positive():
    assert tg.api_version() >= 1


def test_build_info_has_expected_fields():
    info = tg.build_info()
    assert set(info) == {"git_sha", "git_dirty", "build_date"}
    assert info["git_sha"]
    assert isinstance(info["git_dirty"], bool)
