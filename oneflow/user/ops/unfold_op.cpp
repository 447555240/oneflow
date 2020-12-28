/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/framework/framework.h"
#include "oneflow/user/ops/nn_util.h"
#include "oneflow/core/operator/operator_util.h"

namespace oneflow {

namespace user_op {

namespace {

typedef std::function<Maybe<void>(user_op::InferContext* ctx)> TensorDescInferFn;
typedef std::function<void(const user_op::UserOpWrapper& op, user_op::AddOpFn AddOp)>
    GenBackwardOpConfFn;

TensorDescInferFn MakeFwTensorDescInferFn() {
  return [](user_op::InferContext* ctx) -> Maybe<void> {
    const Shape& x_shape = ctx->TensorDesc4ArgNameAndIndex("x", 0)->shape();
    const int32_t spatial_ndim = x_shape.NumAxes() - 2;
    const std::string& data_format = ctx->Attr<std::string>("data_format");
    std::vector<int32_t> padding_before = ctx->Attr<std::vector<int32_t>>("padding_before");
    std::vector<int32_t> padding_after = ctx->Attr<std::vector<int32_t>>("padding_after");
    const std::vector<int32_t>& kernel_size = ctx->Attr<std::vector<int32_t>>("kernel_size");
    const std::vector<int32_t>& strides = ctx->Attr<std::vector<int32_t>>("strides");
    const std::vector<int32_t>& dilation_rate = ctx->Attr<std::vector<int32_t>>("dilation_rate");
    const int32_t idx_offset = IdxOffset(data_format);
    const size_t c_dim = data_format == "channels_first" ? 1 : spatial_ndim + 1;

    CHECK_GE_OR_RETURN(spatial_ndim, 1);
    CHECK_LE_OR_RETURN(spatial_ndim, 3);
    CHECK_EQ_OR_RETURN(kernel_size.size(), spatial_ndim);
    for (int32_t kernel_dim : kernel_size) { CHECK_GT_OR_RETURN(kernel_dim, 0); }
    CHECK_EQ_OR_RETURN(strides.size(), spatial_ndim);
    for (int32_t stride_dim : strides) { CHECK_GT_OR_RETURN(stride_dim, 0); }
    CHECK_EQ_OR_RETURN(dilation_rate.size(), spatial_ndim);
    for (int32_t dilation_dim : dilation_rate) { CHECK_GT_OR_RETURN(dilation_dim, 0); }

    std::vector<int64_t> dhw_shape(spatial_ndim);
    for (int32_t i = 0; i < spatial_ndim; ++i) {
      dhw_shape[i] = (x_shape.At(idx_offset + i) + padding_before[i] + padding_after[i]
                      - dilation_rate[i] * (kernel_size[i] - 1) - 1)
                         / strides[i]
                     + 1;
    }
    DimVector y_shape(3);
    y_shape.at(0) = x_shape.At(0);
    y_shape.at(1) =
        x_shape.At(c_dim)
        * std::accumulate(kernel_size.begin(), kernel_size.end(), 1, std::multiplies<int>());
    y_shape.at(2) = std::accumulate(dhw_shape.begin(), dhw_shape.end(), 1, std::multiplies<int>());

    user_op::TensorDesc* y_desc = ctx->TensorDesc4ArgNameAndIndex("y", 0);
    *y_desc = *ctx->TensorDesc4ArgNameAndIndex("x", 0);
    *y_desc->mut_shape() = Shape(y_shape);
    return Maybe<void>::Ok();
  };
}

Maybe<void> BwTensorDescInferFn(user_op::InferContext* ctx) {
  *ctx->TensorDesc4ArgNameAndIndex("dx", 0) = *ctx->TensorDesc4ArgNameAndIndex("x", 0);
  return Maybe<void>::Ok();
}

Maybe<void> FwBatchAxisInferFn(user_op::BatchAxisContext* ctx) {
  *ctx->BatchAxis4ArgNameAndIndex("y", 0) = *ctx->BatchAxis4ArgNameAndIndex("x", 0);
  return Maybe<void>::Ok();
}

Maybe<void> BwBatchAxisInferFn(user_op::BatchAxisContext* ctx) {
  *ctx->BatchAxis4ArgNameAndIndex("dx", 0) = *ctx->BatchAxis4ArgNameAndIndex("x", 0);
  return Maybe<void>::Ok();
}

Maybe<void> FwGetSbpFn(user_op::SbpContext* ctx) {
  const std::string& data_format = ctx->Attr<std::string>("data_format");

  ctx->NewBuilder().Split(user_op::OpArg("x", 0), 0).Split(user_op::OpArg("y", 0), 0).Build();
  if (data_format == "channels_first") {
    ctx->NewBuilder().Split(user_op::OpArg("x", 0), 1).Split(user_op::OpArg("y", 0), 1).Build();
  }
  return Maybe<void>::Ok();
}

Maybe<void> BwGetSbpFn(user_op::SbpContext* ctx) {
  const std::string& data_format = ctx->Attr<std::string>("data_format");

  ctx->NewBuilder()
      .Split(user_op::OpArg("x", 0), 0)
      .Split(user_op::OpArg("y", 0), 0)
      .Split(user_op::OpArg("dy", 0), 0)
      .Split(user_op::OpArg("dx", 0), 0)
      .Build();
  if (data_format == "channels_first") {
    ctx->NewBuilder()
        .Split(user_op::OpArg("x", 0), 1)
        .Split(user_op::OpArg("y", 0), 1)
        .Split(user_op::OpArg("dy", 0), 1)
        .Split(user_op::OpArg("dx", 0), 1)
        .Build();
  }
  return Maybe<void>::Ok();
}

GenBackwardOpConfFn MakeGenBackwardOpConfFn() {
  return [](const user_op::UserOpWrapper& op, user_op::AddOpFn AddOp) {
    if (op.NeedGenGradTensor4OpInput("x", 0)) {
      user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
      user_op::UserOpConfWrapper grad_op =
          builder.Op("unfold_grad")
              .Input("x", op.input("x", 0))
              .Input("y", op.output("y", 0))
              .Input("dy", op.GetGradTensorWithOpOutput("y", 0))
              .Output("dx")
              .Attr("data_format", op.attr<std::string>("data_format"))
              .Attr("padding_before", op.attr<std::vector<int32_t>>("padding_before"))
              .Attr("padding_after", op.attr<std::vector<int32_t>>("padding_after"))
              .Attr("kernel_size", op.attr<std::vector<int32_t>>("kernel_size"))
              .Attr("strides", op.attr<std::vector<int32_t>>("strides"))
              .Attr("dilation_rate", op.attr<std::vector<int32_t>>("dilation_rate"))
              .Build();
      op.BindGradTensorWithOpInput(grad_op.output("dx", 0), "x", 0);
      AddOp(grad_op);
    }
  };
}

}  // namespace

REGISTER_USER_OP("unfold")
    .Input("x")
    .Output("y")
    .Attr<std::string>("data_format")
    .Attr<std::vector<int32_t>>("kernel_size")
    .Attr<std::vector<int32_t>>("padding_before")
    .Attr<std::vector<int32_t>>("padding_after")
    .Attr<std::vector<int32_t>>("strides")
    .Attr<std::vector<int32_t>>("dilation_rate")
    .SetTensorDescInferFn(MakeFwTensorDescInferFn())
    .SetBatchAxisInferFn(FwBatchAxisInferFn)
    .SetGetSbpFn(FwGetSbpFn);

REGISTER_USER_OP("unfold_grad")
    .Input("x")
    .Input("y")
    .Input("dy")
    .Output("dx")
    .Attr<std::string>("data_format")
    .Attr<std::vector<int32_t>>("kernel_size")
    .Attr<std::vector<int32_t>>("padding_before")
    .Attr<std::vector<int32_t>>("padding_after")
    .Attr<std::vector<int32_t>>("strides")
    .Attr<std::vector<int32_t>>("dilation_rate")
    .SetTensorDescInferFn(BwTensorDescInferFn)
    .SetBatchAxisInferFn(BwBatchAxisInferFn)
    .SetGetSbpFn(BwGetSbpFn);

REGISTER_USER_OP_GRAD("unfold").SetGenBackwardOpConfFn(MakeGenBackwardOpConfFn());

}  // namespace user_op

}  // namespace oneflow