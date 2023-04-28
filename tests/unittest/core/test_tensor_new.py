"""Test binary operators."""
# pylint: disable=missing-function-docstring, redefined-outer-name, unused-import
import torch
import numpy as np
import pytest
from torch_musa import testing

data_type = [torch.float32, torch.int32, torch.int64, torch.float64]

input_data = [
    {"input": np.array(5.0)},
    {"input": np.random.randn(1)},
    {"input": np.random.randn(1, 2)},
    {"input": np.random.randn(1, 2, 3)},
    {"input": np.random.randn(1, 2, 3)},
    {"input": np.random.randn(1, 0, 3)},
    {"input": np.random.randn(1, 2, 3, 4)},
    {"input": np.random.randn(1, 2, 3, 4, 3)},
    {"input": np.random.randn(1, 2, 3, 4, 3, 2)},
    {"input": np.random.randn(1, 2, 3, 4, 3, 2, 4)},
    {"input": np.random.randn(1, 2, 3, 4, 3, 2, 4, 2)},
]


@pytest.mark.parametrize("input_data", input_data)
@pytest.mark.parametrize("data_type", data_type)
def test_tensor_new(input_data, data_type):
    test = testing.OpTest(func=torch.Tensor.new, input_args={})
    test.check_result(torch.tensor(data=input_data["input"], dtype=data_type))


@pytest.mark.parametrize("input_data", input_data)
@pytest.mark.parametrize("data_type", data_type)
def test_tensor_a_new(input_data, data_type):
    mtgpu_tensor = torch.tensor(
        data=input_data["input"], dtype=data_type, device="musa"
    )
    new_mtgpu_result = mtgpu_tensor.new(input_data["input"]).cpu().detach().numpy()

    cpu_tensor = torch.tensor(data=input_data["input"], dtype=data_type, device="cpu")
    new_cpu_result = cpu_tensor.new(input_data["input"]).detach().numpy()

    assert new_mtgpu_result.shape == new_cpu_result.shape
    assert new_mtgpu_result.dtype == new_cpu_result.dtype
