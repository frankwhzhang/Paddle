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

#pragma once

#include <vector>

#include <boost/preprocessor/arithmetic/div.hpp>
#include <boost/preprocessor/arithmetic/mod.hpp>
#include <boost/preprocessor/comparison/greater.hpp>
#include <boost/preprocessor/comparison/greater_equal.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include "paddle/fluid/framework/eigen.h"
#include "paddle/fluid/framework/op_registry.h"
#include "paddle/fluid/framework/operator.h"

#define MAX_RANK_SUPPORTED 6

#define EXPAND_AS_TEMPLATE(z, n, data) \
  case n + 1: {                     \
    ExpandAs<n + 1>(context);         \
    break;                          \
  }
#define REP_EXPAND_AS_TEMPLATE(n) BOOST_PP_REPEAT(n, EXPAND_AS_TEMPLATE, ~)
#define COND(n)                                               \
  BOOST_PP_GREATER_EQUAL(BOOST_PP_DIV(n, MAX_RANK_SUPPORTED), \
                         BOOST_PP_MOD(n, MAX_RANK_SUPPORTED))
#define EXPAND_AS_GRAD_CASE(n)                                        \
  case n: {                                                        \
    ExpandAsBackward<n>(context, reshape_dims_vec, reduce_dims_vec); \
    break;                                                         \
  }
#define EXPAND_AS_GRAD_TEMPLATE(z, n, data) \
  BOOST_PP_IF(COND(n), EXPAND_AS_GRAD_CASE(n), )
#define REP_EXPAND_AS_GRAD_TEMPLATE(n) BOOST_PP_REPEAT(n, EXPAND_AS_GRAD_TEMPLATE, ~)

namespace paddle {
namespace operators {

using Tensor = framework::Tensor;
template <typename T, int MajorType = Eigen::RowMajor,
          typename IndexType = Eigen::DenseIndex>
using EigenVector = framework::EigenVector<T, MajorType, IndexType>;
template <typename T, size_t D, int MajorType = Eigen::RowMajor,
          typename IndexType = Eigen::DenseIndex>
using EigenTensor = framework::EigenTensor<T, D, MajorType, IndexType>;

template <typename DeviceContext, typename T>
class ExpandAsKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& context) const override {
    auto rank = context.Input<Tensor>("X")->dims().size();
    switch (rank) {
      REP_EXPAND_AS_TEMPLATE(MAX_RANK_SUPPORTED)
      default:
        PADDLE_ENFORCE(false,
                       "Only support tensor with rank being between 1 and 6.");
    }
  }

 protected:
  template <int Rank>
  void ExpandAs(const framework::ExecutionContext& context) const {
    auto* in0 = context.Input<Tensor>("X");
    auto in_dims = in0->dims();
    auto* expand_tensor = context.Input<Tensor>("expand_tensor");
    auto* out0 = context.Output<Tensor>("Out");
    Eigen::DSizes<int, Rank> bcast_dims;
    int bcast_dims_remainder = 0;
    auto x_dims = in0->dims();
    auto y_dims = expand_tensor->dims();
    for (int i = 0; i < y_dims.size(); ++i) {
      bcast_dims[i] = y_dims[i]/x_dims[i];
      bcast_dims_remainder += y_dims[i]%x_dims[i];
    }
   PADDLE_ENFORCE_EQ(
          bcast_dims_remainder, 0,
          "X(input) could not be broadcast together with remapped shape(expand tensor's shape)");
    framework::DDim out_dims(in_dims);
    for (size_t i = 0; i < bcast_dims.size(); ++i) {
      out_dims[i] *= bcast_dims[i];
    }

    out0->Resize(out_dims);
    auto x = EigenTensor<T, Rank>::From(*in0);
    out0->mutable_data<T>(context.GetPlace());
    auto y = EigenTensor<T, Rank>::From(*out0);
    auto& place =
        *context.template device_context<DeviceContext>().eigen_device();
    y.device(place) = x.broadcast(bcast_dims);
  }
};

template <typename DeviceContext, typename T>
class ExpandAsGradKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& context) const override {
    auto* in0 = context.Input<Tensor>("X");
    auto* expand_tensor = context.Input<Tensor>("expand_tensor");
    auto x_dims = in0->dims();
    auto y_dims = expand_tensor->dims();
    std::vector<int> bcast_dims;
    for (int i = 0; i < y_dims.size(); ++i) {
        bcast_dims.push_back(y_dims[i]/x_dims[i]);
    }
    std::vector<int> reshape_dims_vec;
    std::vector<int> reduce_dims_vec;
    for (size_t i = 0; i < bcast_dims.size(); ++i) {
      if (bcast_dims[i] == 1) {
        reshape_dims_vec.push_back(x_dims[i]);
      } else {
        if (x_dims[i] == 1) {
          reduce_dims_vec.push_back(reshape_dims_vec.size());
          reshape_dims_vec.push_back(bcast_dims[i]);
        } else {
          reduce_dims_vec.push_back(reshape_dims_vec.size());
          reshape_dims_vec.push_back(bcast_dims[i]);
          reshape_dims_vec.push_back(x_dims[i]);
        }
      }
    }
    int dims = reshape_dims_vec.size() * MAX_RANK_SUPPORTED +
               reduce_dims_vec.size() - MAX_RANK_SUPPORTED - 1;
    // no need reduce, just copy
    if (reduce_dims_vec.size() == 0) {
      auto* in0 = context.Input<Tensor>(framework::GradVarName("Out"));
      auto* out0 = context.Output<Tensor>(framework::GradVarName("X"));
      out0->mutable_data<T>(context.GetPlace());
      framework::TensorCopy(*in0, context.GetPlace(), context.device_context(),
                            out0);
    } else {
      switch (dims) {
        REP_EXPAND_AS_GRAD_TEMPLATE(72)
        default:
          PADDLE_ENFORCE(
              false, "Only support tensor with rank being between 1 and 6.");
      }
    }
  }

 protected:
  template <int Dims>
  void ExpandAsBackward(const framework::ExecutionContext& context,
                      const std::vector<int>& reshape_dims_vec,
                      const std::vector<int>& reduce_dims_vec) const {
    size_t reshape_size = Dims / MAX_RANK_SUPPORTED + 1;
    size_t reduce_size = Dims % MAX_RANK_SUPPORTED + 1;
    PADDLE_ENFORCE_EQ(reshape_size, reshape_dims_vec.size(),
                      "Inconsistent size between template Dims and "
                      "reshape dimensions.");
    PADDLE_ENFORCE_EQ(reduce_size, reduce_dims_vec.size(),
                      "Inconsistent size between template Dims and "
                      "reduce dimensions.");
    auto* in0 = context.Input<Tensor>(framework::GradVarName("Out"));
    auto* out0 = context.Output<Tensor>(framework::GradVarName("X"));
    out0->mutable_data<T>(context.GetPlace());
    auto x_grad = EigenVector<T>::Flatten(*out0);
    Eigen::DSizes<int, Dims / MAX_RANK_SUPPORTED + 1> reshape_dims;
    for (size_t i = 0; i < reshape_size; ++i) {
      reshape_dims[i] = reshape_dims_vec[i];
    }
    Eigen::DSizes<int, Dims % MAX_RANK_SUPPORTED + 1> reduce_dims;
    for (size_t i = 0; i < reduce_size; ++i) {
      reduce_dims[i] = reduce_dims_vec[i];
    }
    auto out_grad = EigenVector<T>::Flatten(*in0);
    x_grad.device(
        *context.template device_context<DeviceContext>().eigen_device()) =
        out_grad.reshape(reshape_dims).sum(reduce_dims).reshape(x_grad.dimensions());
  }
};

}  // namespace operators
}  // namespace paddle