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
#include "oneflow/core/graph/boxing/b21_sub_task_graph_builder.h"
#include "oneflow/core/graph/boxing/sub_task_graph_builder_util.h"

namespace oneflow {

Maybe<SubTskGphBuilderStatus> B21SubTskGphBuilder::Build(
    SubTskGphBuilderCtx* ctx, const std::vector<TaskNode*>& sorted_src_tasks,
    const std::vector<TaskNode*>& sorted_dst_tasks, const ParallelDesc& src_parallel_desc,
    const ParallelDesc& dst_parallel_desc, const LogicalBlobId& lbi,
    const BlobDesc& logical_blob_desc, const SbpParallel& src_sbp_parallel,
    const SbpParallel& dst_sbp_parallel, const Shape& time_shape) const {
  if ((src_parallel_desc.parallel_num() == 1 || src_sbp_parallel.has_broadcast_parallel())
      && dst_parallel_desc.parallel_num() == 1) {
    TaskNode* dst_node = sorted_dst_tasks.front();
    TaskNode* nearest_src_node = SubTskGphBuilderUtil::FindNearestNode(sorted_src_tasks, dst_node);
    CHECK_NOTNULL(nearest_src_node);
    TaskNode* proxy = ctx->GetProxyNode(nearest_src_node, nearest_src_node->MemZoneId121(),
                                        dst_node->machine_id(), dst_node->MemZoneId121());
    Connect<TaskNode>(proxy, ctx->task_graph()->NewEdge(), dst_node);
    return TRY(BuildSubTskGphBuilderStatus(
        sorted_src_tasks.front(), sorted_dst_tasks.front(), src_parallel_desc, dst_parallel_desc,
        src_sbp_parallel, dst_sbp_parallel, lbi, logical_blob_desc, "B21SubTskGphBuilder", ""));
  } else {
    return Error::BoxingNotSupportedError();
  }
}

}  // namespace oneflow
