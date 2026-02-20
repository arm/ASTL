import pytest

import astl


def test_save_collection_api_exists_and_is_callable():
    assert hasattr(astl, "save_collection")
    assert callable(astl.save_collection)


def test_load_collection_api_exists_and_is_callable():
    assert hasattr(astl, "load_collection")
    assert callable(astl.load_collection)


def test_load_collection_rejects_empty_path():
    with pytest.raises(ValueError, match="non-empty string"):
        astl.load_collection("")


def test_load_collection_rejects_negative_chunk_size():
    with pytest.raises(ValueError, match=">= 0"):
        astl.load_collection("/tmp/fake.astl", chunk_size_bytes=-1)


def test_load_collection_nonexistent_file_raises_astl_error():
    with pytest.raises(astl.ASTLError):
        astl.load_collection("/tmp/astl_python_wrapper_nonexistent.astl")


def test_save_collection_accepts_none_or_string_path():
    # save_collection(None) is valid at API layer; backend may still return ASTLError
    # depending on runtime state (e.g. not initialized / no data).
    try:
        astl.save_collection(None)
    except astl.ASTLError:
        pass

    try:
        astl.save_collection("/tmp/astl_python_wrapper_save.astl")
    except astl.ASTLError:
        pass
