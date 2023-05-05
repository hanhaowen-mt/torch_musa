"""Test bmm operators."""
# pylint: disable=missing-function-docstring, redefined-outer-name, unused-import
import torch
import pytest
import torch_musa

from torch_musa import testing
input_data = [
            {
             'input': torch.randn(4, 10, 5),
             'mat2': torch.randn(4, 5, 10),
             },
            {
             'input': torch.randn(4, 10, 5).transpose(1, 2),
             'mat2': torch.randn(4, 5, 10).transpose(1, 2),
             },
             {
             'input': torch.randn(4, 10, 5).transpose(0, 1),
             'mat2': torch.randn(5, 10, 10).transpose(0, 1),
             },

    ]


@pytest.mark.parametrize("input_data", input_data)
def test_bmm(input_data):
    test = testing.OpTest(
        func=torch.bmm,
        input_args=input_data
    )
    test.check_result()