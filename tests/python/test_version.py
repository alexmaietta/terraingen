import terraingen as tg


def test_version_matches_version_file(repo_root):
    expected = (repo_root / "VERSION").read_text().strip()
    assert tg.__version__ == expected
    assert ".".join(str(part) for part in tg.version()) == expected


def test_api_version_is_positive():
    assert tg.api_version() >= 1


def test_build_info_has_expected_fields():
    info = tg.build_info()
    assert set(info) == {"git_sha", "git_dirty", "build_date"}
    assert info["git_sha"]
    assert isinstance(info["git_dirty"], bool)
