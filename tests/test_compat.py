import sys
from pathlib import Path

import pytest
import torch

from worker.compat import preserve_normalization_dtypes

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".deps/CorridorKey2.0"))
from corridorkey.blocks import RMSNorm


@pytest.mark.parametrize("norm", [torch.nn.RMSNorm, RMSNorm])
def test_rmsnorm_preserves_bfloat16_under_cuda_autocast(norm):
    model = torch.nn.Sequential(norm(8)).cuda()
    x = torch.linspace(-2, 2, 64, device="cuda", dtype=torch.bfloat16).reshape(2, 4, 8)
    expected = model(x.float()).to(x.dtype)
    preserve_normalization_dtypes(model)
    with torch.autocast("cuda", dtype=torch.bfloat16):
        actual = model(x)
    assert actual.dtype == x.dtype
    torch.testing.assert_close(actual, expected)
    assert model(x.float()).dtype == torch.float32
