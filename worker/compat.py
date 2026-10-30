"""Compatibility at the upstream model boundary."""

import torch


def _match_input_dtype(_module, inputs, output):
    return output.to(dtype=inputs[0].dtype)


def preserve_normalization_dtypes(model):
    from corridorkey.blocks import LayerNorm2d, RMSNorm

    # PyTorch 2.12+ promotes CUDA RMSNorm to float32 under autocast. CorridorKey's
    # region attention allocates from Q's dtype but SDPA still returns bfloat16.
    # Restore the pre-2.12 output contract per model instance, preserving the
    # normalization calculation and leaving PyTorch/upstream source untouched.
    for module in model.modules():
        if isinstance(module, (torch.nn.RMSNorm, RMSNorm, LayerNorm2d)) and not getattr(
                module, "_ck_dtype_hook", False):
            module.register_forward_hook(_match_input_dtype)
            module._ck_dtype_hook = True
    return model
