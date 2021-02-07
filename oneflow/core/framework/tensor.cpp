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
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/register/blob.h"
#include "oneflow/core/job/job_build_and_infer_ctx_mgr.h"
#include "oneflow/core/common/to_string.h"

namespace oneflow {

namespace user_op {

#ifdef WITH_CUDA

template<>
void Tensor::CheckDataType<half>() const {
  LOG_IF(FATAL, data_type() != DataType::kFloat16)
      << "tensor data_type mismatched. value: kFloat16, template T: half";
}

#endif  // WITH_CUDA

}  // namespace user_op

namespace one {

Device::Device(DeviceType device_type, int64_t device_id)
    : device_id_(device_id), device_type_(device_type) {
  CHECK(legal());
}

bool Device::legal() const {
  if (device_type() == DeviceType::kCPU && device_id() != 0) { return false; }
  return true;
}

std::string Device::device_type_str() const {
  std::string device_type_str = CHECK_JUST(DeviceTag4DeviceType(device_type()));
  if (device_type_str == "gpu") { device_type_str = "cuda"; }
  return device_type_str;
}

std::string Device::ToString() const {
  std::stringstream ss;
  ss << "device(type=";
  ss << device_type_str();
  ss << ", index=";
  ss << device_index_str();
  ss << ")";
  return ss.str();
}

bool Tensor::is_lazy() const { return !EagerExecutionEnabled(); }

MirroredTensor::MirroredTensor(const std::shared_ptr<Shape>& shape, DataType dtype,
                               const std::shared_ptr<Device>& device) {
  if (is_lazy()) {
    impl_ = std::make_shared<MirroredLazyTensorImpl>(shape, dtype, device);
  } else {
    impl_ = std::make_shared<MirroredEagerTensorImpl>(shape, dtype, device);
  }
}

MirroredTensor::MirroredTensor(const std::shared_ptr<cfg::LogicalBlobId>& lbi,
                               const std::string& job_name,
                               const std::shared_ptr<compatible_py::Distribute>& distribute) {
  impl_ = std::make_shared<MirroredLazyBlob>(lbi, job_name, distribute);
}

MirroredTensor::MirroredTensor(const std::shared_ptr<cfg::LogicalBlobId>& lbi,
                               const std::shared_ptr<compatible_py::BlobObject>& blob_object,
                               const std::shared_ptr<compatible_py::BlobRegister>& blob_register,
                               const std::string& job_name,
                               const std::shared_ptr<compatible_py::Distribute>& distribute) {
  impl_ =
      std::make_shared<MirroredEagerBlob>(lbi, blob_object, blob_register, job_name, distribute);
}

ConsistentTensor::ConsistentTensor(const std::shared_ptr<Shape>& shape, DataType dtype,
                                   const std::shared_ptr<compatible_py::Distribute>& distribute,
                                   std::shared_ptr<cfg::ParallelConf>& parallel_conf) {
  if (is_lazy()) {
    impl_ = std::make_shared<ConsistentLazyTensorImpl>(shape, dtype, distribute, parallel_conf);
  } else {
    impl_ = std::make_shared<ConsistentEagerTensorImpl>(shape, dtype, distribute, parallel_conf);
  }
}

ConsistentTensor::ConsistentTensor(const std::shared_ptr<cfg::LogicalBlobId>& lbi,
                                   const std::string& job_name,
                                   const std::shared_ptr<compatible_py::Distribute>& distribute) {
  impl_ = std::make_shared<ConsistentLazyBlob>(lbi, job_name, distribute);
}

ConsistentTensor::ConsistentTensor(
    const std::shared_ptr<cfg::LogicalBlobId>& lbi,
    const std::shared_ptr<compatible_py::BlobObject>& blob_object,
    const std::shared_ptr<compatible_py::BlobRegister>& blob_register, const std::string& job_name,
    const std::shared_ptr<compatible_py::Distribute>& distribute) {
  impl_ =
      std::make_shared<ConsistentEagerBlob>(lbi, blob_object, blob_register, job_name, distribute);
}
}  // namespace one

}  // namespace oneflow
