from jamre.jumptables import find_switch_tables
from .synth import switch_image


def bodies(n):
    return [f"mov eax, {k * 7 + 1}\n ret" for k in range(n)]


def test_dense_switch_found_with_bound_and_targets():
    img, rva = switch_image(bodies(6))
    tables = find_switch_tables(img, min_cases=4)
    assert len(tables) == 1
    t = tables[0]
    assert t.table_rva == rva and t.cases == 6 and t.bound_from_cmp
    assert len(set(t.targets)) == 6 and all(img.is_code(x) for x in t.targets)
    assert t.byte_table_rva is None


def test_sparse_switch_uses_byte_index_table():
    # 10 case slots mapped onto 3 bodies
    idx = [0, 0, 1, 1, 1, 2, 2, 0, 1, 2]
    img, rva = switch_image(bodies(3), sparse=idx)
    t = find_switch_tables(img, min_cases=4)[0]
    assert t.cases == 10 and t.byte_table_rva is not None
    assert t.distinct_targets() == 3
    assert [t.targets[i] for i in (0, 2, 5)] == [img.u32(rva), img.u32(rva + 4), img.u32(rva + 8)]


def test_non_code_run_is_not_a_table():
    img, _ = switch_image(bodies(6))
    # a dword load whose table holds data, not code addresses, must be ignored
    img.sections[1].data = bytes(len(img.sections[1].data))
    assert find_switch_tables(img, min_cases=4) == []
