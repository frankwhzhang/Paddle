/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/fluid/operators/expand_as_op.h"
#include <memory>
#include <vector>

namespace paddle {
namespace operators {

using framework::Tensor;

class ExpandAsOp : public framework::OperatorWithKernel {
 public:
  using framework::OperatorWithKernel::OperatorWithKernel;

 protected:
  void InferShape(framework::InferShapeContext* ctx) const override {
    PADDLE_ENFORCE(ctx->HasInput("X"), "Input(X) should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("expand_tensor"), "Input(expand_tensor) should not be null.");
    PADDLE_ENFORCE(ctx->HasOutput("Out"), "Output(Out) should not be null.");
    auto x_dims = ctx->GetInputDim("X");
    auto expand_tensor_dims = ctx->GetInputDim("expand_tensor");
    PADDLE_ENFORCE_EQ(static_cast<size_t>(x_dims.size()), expand_tensor_dims.size(),
                      "The rank of input(expand_tensor) must be equal "
                     "to the rank of Input(X).");
    PADDLE_ENFORCE_LE(x_dims.size(), 6,
                      "The rank of Input(X) must not be greater than 6.");
    std::vector<int64_t> out_shape(x_dims.size());
    ctx->SetOutputDim("Out", framework::make_ddim(out_shape));
  }

  protected:
  framework::OpKernelType GetExpectedKernelType(
      const framework::ExecutionContext& ctx) const override {
    return framework::OpKernelType(ctx.Input<Tensor>("X")->type(),
                                   ctx.device_context());
  }

  framework::OpKernelType GetKernelTypeForVar(
      const std::string& var_name, const Tensor& tensor,
      const framework::OpKernelType& expected_kernel_type) const override {
    if (var_name == "expand_tensor") {
      return expected_kernel_type;
    }
    return framework::OpKernelType(expected_kernel_type.data_type_,
                                   tensor.place(), tensor.layout());
  }
};

class ExpandAsOpMaker : public framework::OpProtoAndCheckerMaker {
 public:
  void Make() override {
    AddInput("X",
             "(Tensor, default Tensor<float>). A tensor with rank in [1, 6]."
             "X is the input to be expanded.");
    AddOutput("Out",
              "(Tensor, default Tensor<float>). A tensor with rank in [1, 6]."
              "The rank of Output(Out) have the same with Input(X). "
              "After expanding, size of each dimension of Output(Out) is equal "
              "to size of the corresponding dimension of Input(X) multiplying "
              "the corresponding value given by Attr(expand_times).");
    AddInput("expand_tensor",
                              "Expand tensor's shape for each dimension.");
    AddComment(R"DOC(
Expand operator tiles the input by given times number. You should set times
number for each dimension by providing tensor 'expend_tensor'. The rank of X
should be in [1, 6]. Please note that size of 'expend_tensor' must be the same
with X's rank. Following is a using case:
Input(X) is a 3-D tensor with shape [2, 3, 1]:
        [
           [[1], [2], [3]],
           [[4], [5], [6]]
        ]
expand_tensors'shape:  [1, 2, 2]
Output(Out) is a 3-D tensor with shape [2, 6, 2]:
        [
            [[1, 1], [2, 2], [3, 3], [1, 1], [2, 2], [3, 3]],
            [[4, 4], [5, 5], [6, 6], [4, 4], [5, 5], [6, 6]]
        ]
)DOC");
  }
};

class ExpandAsGradOp : public framework::OperatorWithKernel {
 public:
  using framework::OperatorWithKernel::OperatorWithKernel;

 protected:
  void InferShape(framework::InferShapeContext* ctx) const override {
    PADDLE_ENFORCE(ctx->HasInput("X"), "Input(X) should not be null.");
    PADDLE_ENFORCE(ctx->HasInput(framework::GradVarName("Out")),
                   "Input(Out@GRAD) should not be null.");

    auto x_dims = ctx->GetInputDim("X");
    auto out_dims = ctx->GetInputDim(framework::GradVarName("Out"));
    if (!ctx->IsRuntime() && x_dims[0] < 0) {
      PADDLE_ENFORCE_EQ(
          x_dims[0], out_dims[0],
          "The first dimension size of Input(Out@GRAD) should be "
          "equal to the crroresponding dimension size of Input(X)");
    }
    auto x_grad_name = framework::GradVarName("X");
    if (ctx->HasOutput(x_grad_name)) {
      ctx->SetOutputDim(x_grad_name, x_dims);
    }
  }
 protected:
  framework::OpKernelType GetExpectedKernelType(
      const framework::ExecutionContext& ctx) const override {
    return framework::OpKernelType(
        ctx.Input<Tensor>(framework::GradVarName("Out"))->type(),
        ctx.device_context());
  }

  framework::OpKernelType GetKernelTypeForVar(
      const std::string& var_name, const Tensor& tensor,
      const framework::OpKernelType& expected_kernel_type) const override {
    if (var_name == "expand_tensor") {
      return expected_kernel_type;
    }
    return framework::OpKernelType(expected_kernel_type.data_type_,
                                   tensor.place(), tensor.layout());
  }
};

class ExpandAsGradOpDescMaker : public framework::SingleGradOpDescMaker {
 public:
  using framework::SingleGradOpDescMaker::SingleGradOpDescMaker;

 protected:
  std::unique_ptr<framework::OpDesc> Apply() const override {
    std::unique_ptr<framework::OpDesc> op(new framework::OpDesc());
    op->SetType("expand_as_grad");
    op->SetInput("X", Input("X"));
    op->SetInput("expand_tensor", Input("expand_tensor"));
    op->SetInput(framework::GradVarName("Out"), OutputGrad("Out"));
    op->SetOutput(framework::GradVarName("X"), InputGrad("X"));
    op->SetAttrMap(Attrs());
    return op;
  }
};
    
//DECLARE_NO_NEED_BUFFER_VARS_INFERENCE(ExpandGradNoNeedBufVarsInferer, "X");

}  // namespace operators
}  // namespace paddle

namespace ops = paddle::operators;
REGISTER_OPERATOR(expand_as, ops::ExpandAsOp, ops::ExpandAsOpMaker,ops::ExpandAsGradOpDescMaker);
REGISTER_OPERATOR(expand_as_grad, ops::ExpandAsGradOp);
REGISTER_OP_CPU_KERNEL(
    expand_as, ops::ExpandAsKernel<paddle::platform::CPUDeviceContext, float>,
    ops::ExpandAsKernel<paddle::platform::CPUDeviceContext, double>,
    ops::ExpandAsKernel<paddle::platform::CPUDeviceContext, int>,
    ops::ExpandAsKernel<paddle::platform::CPUDeviceContext, bool>
);
REGISTER_OP_CPU_KERNEL(
    expand_as_grad,
    ops::ExpandAsGradKernel<paddle::platform::CPUDeviceContext, float>,
    ops::ExpandAsGradKernel<paddle::platform::CPUDeviceContext, double>
);
