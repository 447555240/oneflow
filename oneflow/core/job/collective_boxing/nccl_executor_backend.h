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
#ifndef ONEFLOW_CORE_JOB_COLLECTIVE_BOXING_NCCL_EXECUTOR_BACKEND_H_
#define ONEFLOW_CORE_JOB_COLLECTIVE_BOXING_NCCL_EXECUTOR_BACKEND_H_

#ifdef WITH_CUDA

#include "oneflow/core/job/collective_boxing/executor.h"
#include "oneflow/core/thread/thread_pool.h"
#include "oneflow/core/graph/boxing/collective_boxing_util.h"

namespace oneflow {

namespace boxing {

namespace collective {

class NcclExecutorBackend : public ExecutorBackend {
 public:
  OF_DISALLOW_COPY_AND_MOVE(NcclExecutorBackend)
  NcclExecutorBackend();
  ~NcclExecutorBackend() override;

 private:
  void Init(const CollectiveBoxingPlan& collective_boxing_plan,
            std::shared_ptr<RequestStore> request_store) override;
  void GroupRequests(const std::vector<int32_t>& request_ids,
                     std::vector<std::vector<int32_t>>* groups) override;
  void ExecuteRequests(const std::vector<int32_t>& request_ids) override;

  int32_t num_devices_;
  int64_t num_streams_;
  int64_t fusion_threshold_;
  const CollectiveBoxingConf collective_boxing_conf_;

  int64_t current_stream_id_ = 0;
  std::shared_ptr<RequestStore> request_store_;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace collective

}  // namespace boxing

}  // namespace oneflow

#endif  // WITH_CUDA

#endif  // ONEFLOW_CORE_JOB_COLLECTIVE_BOXING_NCCL_EXECUTOR_BACKEND_H_
