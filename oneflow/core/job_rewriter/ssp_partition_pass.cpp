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
#include "oneflow/core/job_rewriter/op_graph_pass.h"
#include "oneflow/core/job_rewriter/autograd.h"
#include "oneflow/core/job/job_builder.h"
#include "oneflow/core/job/scope.h"
#include "oneflow/core/vm/symbol_storage.h"
#include "oneflow/core/common/util.h"
#include "oneflow/core/common/protobuf.h"
#include "oneflow/core/common/balanced_splitter.h"
#include "oneflow/core/job_rewriter/ssp_partition_conf.pb.h"

namespace oneflow {

namespace {

class SspPartitionStragety {
 public:
  SspPartitionStragety() = default;
  ~SspPartitionStragety() = default;
  virtual Maybe<void> Apply(const JobDesc& job_desc, Job* job) const = 0;
};

class SspPartitionPass final : public OpGraphPass {
 public:
  explicit SspPartitionPass(const JobDesc& job_desc) : OpGraphPass(job_desc) {}
  bool IsEnabled() const override { return job_desc().IsTrain(); }

  Maybe<void> Apply(Job* job) const override {
    const std::string& partition_strategy = job_desc().String("ssp_partition_strategy");
    std::unique_ptr<const SspPartitionStragety> strategy;
    strategy.reset(NewObj<std::string, SspPartitionStragety>(partition_strategy));
    strategy->Apply(job_desc(), job);
    return Maybe<void>::Ok();
  }
};
REGISTER_FUNCTION_PASS("SspPartition", SspPartitionPass);
REGISTER_FUNCTION_CONFIG_DEF().String("ssp_partition_strategy", "disable",
                                      "ssp partition strategy");

#define REGISTER_SSP_PARTITION_STRATEGY(strategy_name, strategy_type)      \
  REGISTER_CLASS_CREATOR(std::string, strategy_name, SspPartitionStragety, \
                         ([] { return new strategy_type(); }));

class DisableSspPartitionStrategy : public SspPartitionStragety {
 public:
  DisableSspPartitionStrategy() = default;
  ~DisableSspPartitionStrategy() = default;

  Maybe<void> Apply(const JobDesc& job_desc, Job* job) const override { return Maybe<void>::Ok(); }
};
REGISTER_SSP_PARTITION_STRATEGY("disable", DisableSspPartitionStrategy);

Maybe<void> ForEachSspScope4TrainableFwOp(
    const OpGraph&, const JobDesc&, const std::function<Maybe<void>(const OpNode*, const Scope&)>&);

class NaiveSequantialSspPartitionStrategy : public SspPartitionStragety {
 public:
  NaiveSequantialSspPartitionStrategy() = default;
  ~NaiveSequantialSspPartitionStrategy() = default;

  Maybe<void> Apply(const JobDesc& job_desc, Job* job) const override {
    const OpGraph op_graph(*job);
    JobBuilder job_builder(job);
    JUST(ForEachSspScope4TrainableFwOp(
        op_graph, job_desc, [&](const OpNode* op_node, const Scope& scope) -> Maybe<void> {
          // Sets scope_symbol_id
          std::vector<OperatorConf> op_confs(1);
          auto* op_conf = &op_confs.at(0);
          op_conf->CopyFrom(op_node->op().op_conf());
          op_conf->set_scope_symbol_id(scope.scope_proto().symbol_id());
          job_builder.MutOpsOnlyOnce(op_confs);
          // Sets parallel_conf
          const auto* parallel_desc = JUST(scope.GetParallelDesc(*op_conf));
          const auto& op_name = op_node->op().op_name();
          job_builder.MutParallelConfOnlyOnce(op_name, parallel_desc->parallel_conf());
          return Maybe<void>::Ok();
        }));
    return Maybe<void>::Ok();
  }
};
REGISTER_SSP_PARTITION_STRATEGY("naive_sequantial", NaiveSequantialSspPartitionStrategy);

Maybe<void> GetSspPartitionConf(const JobDesc& job_desc, SspPartitionConf* ssp_partition_conf);
Maybe<void> GetSequantialTrainableFwOps(
    const OpGraph&, std::list<std::unique_ptr<std::vector<OpNode*>>>* sequantial_trainable_fw_ops);
Maybe<void> GetSspDepth2Stage(
    const std::list<std::unique_ptr<std::vector<OpNode*>>>& sequantial_trainable_fw_ops,
    int64_t num_stages, std::function<Maybe<int64_t>(int64_t)>* Stage4Depth);
Maybe<void> MakeGetterSspScope4Stage(
    const SspPartitionConf& ssp_partition_conf,
    std::function<Maybe<const Scope&>(int64_t stage)>* SspScope4Stage);

Maybe<void> ForEachSspScope4TrainableFwOp(
    const OpGraph& op_graph, const JobDesc& job_desc,
    const std::function<Maybe<void>(const OpNode*, const Scope&)>& Handler) {
  // Sequantialize trainable forward ops
  std::list<std::unique_ptr<std::vector<OpNode*>>> sequantial_trainable_fw_ops;
  JUST(GetSequantialTrainableFwOps(op_graph, &sequantial_trainable_fw_ops));
  // Gets ssp partition config
  SspPartitionConf ssp_partition_conf;
  JUST(GetSspPartitionConf(job_desc, &ssp_partition_conf));
  // Partition to stages
  std::function<Maybe<int64_t>(int64_t)> Stage4Depth;
  int64_t num_stages = ssp_partition_conf.stage_scope_id_size();
  JUST(GetSspDepth2Stage(sequantial_trainable_fw_ops, num_stages, &Stage4Depth));
  std::function<Maybe<const Scope&>(int64_t)> SspScope4Stage;
  // Provides scope for each stage
  JUST(MakeGetterSspScope4Stage(ssp_partition_conf, &SspScope4Stage));
  int64_t depth = 0;
  for (const auto& fused_vec : sequantial_trainable_fw_ops) {
    int64_t stage = JUST(Stage4Depth(depth));
    const auto& scope = JUST(SspScope4Stage(stage));
    for (OpNode* op_node : *fused_vec) { JUST(Handler(op_node, scope)); }
    ++depth;
  }
  return Maybe<void>::Ok();
}

Maybe<void> GetTrainableFwOps(const OpGraph& op_graph, HashSet<OpNode*>* trainable_fw_ops);

Maybe<void> GetBackboneOp2FusedOps(
    const OpGraph& op_graph,
    HashMap<OpNode*, std::unique_ptr<std::vector<OpNode*>>>* backbone_op2fused_ops);

Maybe<void> GetSspPartitionConf(const JobDesc& job_desc, SspPartitionConf* ssp_partition_conf) {
  const std::string& ssp_partition_conf_str = job_desc.String("ssp_partition_conf");
  CHECK_OR_RETURN(TxtString2PbMessage(ssp_partition_conf_str, ssp_partition_conf))
      << "SspPartitionConf parsing failed. ssp_partition_conf_str:\n"
      << ssp_partition_conf_str;
  CHECK_GT_OR_RETURN(ssp_partition_conf->stage_scope_id_size(), 0);
  return Maybe<void>::Ok();
}
REGISTER_FUNCTION_CONFIG_DEF().String("ssp_partition_conf", SspPartitionConf().DebugString(),
                                      "type: serialized SspPartitionConf. ssp parallel config");

Maybe<void> GetSequantialTrainableFwOps(
    const OpGraph& op_graph,
    std::list<std::unique_ptr<std::vector<OpNode*>>>* sequantial_trainable_fw_ops) {
  HashMap<OpNode*, std::unique_ptr<std::vector<OpNode*>>> backbone_op2fused_ops;
  JUST(GetBackboneOp2FusedOps(op_graph, &backbone_op2fused_ops));
  std::list<OpNode*> starts;
  {
    const auto& ForEachOut = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
      node->ForEachNodeOnOutEdge([&](OpNode* out_node) {
        if (backbone_op2fused_ops.count(out_node) > 0) { Handler(out_node); }
      });
    };
    const auto& IsSinkNode = [&](OpNode* node) {
      size_t out_num = 0;
      ForEachOut(node, [&](OpNode*) { ++out_num; });
      return out_num == 0;
    };
    for (const auto& pair : backbone_op2fused_ops) {
      if (IsSinkNode(pair.first)) { starts.push_back(pair.first); }
    }
  }
  const auto& ForEachIn = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
    node->ForEachNodeOnInEdge([&](OpNode* in_node) {
      if (backbone_op2fused_ops.count(in_node) > 0) { Handler(in_node); }
    });
  };
  // Traverses reverserly
  op_graph.BfsForEachNode(starts, ForEachIn, [&](OpNode* op_node) {
    const auto& iter = backbone_op2fused_ops.find(op_node);
    CHECK(iter != backbone_op2fused_ops.end());
    sequantial_trainable_fw_ops->emplace_front(std::move(iter->second));
  });
  return Maybe<void>::Ok();
}

Maybe<void> GetSspDepth2Stage(
    const std::list<std::unique_ptr<std::vector<OpNode*>>>& sequantial_trainable_fw_ops,
    int64_t num_stages, std::function<Maybe<int64_t>(int64_t)>* Stage4Depth) {
  int64_t num_ops = 0;
  for (const auto& vec : sequantial_trainable_fw_ops) { num_ops += vec->size(); }
  BalancedSplitter bs(num_ops, num_stages);
  std::vector<int64_t> stage2expected_num_ops_from_start(num_stages);
  for (int64_t i = 0; i < num_stages; ++i) {
    int64_t last = (i == 0 ? 0 : stage2expected_num_ops_from_start.at(i - 1));
    stage2expected_num_ops_from_start.at(i) = bs.At(i).size() + last;
  }
  auto depth2stage = std::make_shared<HashMap<int64_t, int64_t>>();
  {
    int64_t stage = 0;
    int64_t depth = 0;
    int64_t num_ops_from_start = 0;
    for (const auto& vec : sequantial_trainable_fw_ops) {
      if (num_ops_from_start > stage2expected_num_ops_from_start.at(stage)) { ++stage; }
      (*depth2stage)[depth] = stage;
      ++depth;
      num_ops_from_start += vec->size();
    }
    CHECK_EQ(stage, num_stages - 1);
    CHECK_EQ(depth, sequantial_trainable_fw_ops.size());
  }
  *Stage4Depth = [depth2stage](int64_t depth) -> Maybe<int64_t> {
    const auto& iter = depth2stage->find(depth);
    CHECK_OR_RETURN(iter != depth2stage->end());
    return iter->second;
  };
  return Maybe<void>::Ok();
}

Maybe<void> GetTrainableFwOps(const OpGraph& op_graph, HashSet<OpNode*>* trainable_fw_ops) {
  std::function<bool(OpNode*)> NeedBackwardOp;
  JUST(MakePredicatorNeedBackwardOp(op_graph, &NeedBackwardOp));
  op_graph.ForEachNode([&](OpNode* node) {
    if (NeedBackwardOp(node)) { trainable_fw_ops->insert(node); }
  });
  return Maybe<void>::Ok();
}

Maybe<void> GetBackBoneOps(const OpGraph& op_graph, const HashSet<OpNode*>& trainable_fw_ops,
                           HashSet<OpNode*>* backbone_op_nodes);

Maybe<void> FuseOtherFwOpsToBackboneOps(const OpGraph& op_graph,
                                        const HashSet<OpNode*>& backbone_op_nodes,
                                        HashMap<OpNode*, OpNode*>* other_fw_op2backbone_op);

Maybe<void> GetBackboneOp2FusedOps(
    const OpGraph& op_graph,
    HashMap<OpNode*, std::unique_ptr<std::vector<OpNode*>>>* backbone_op2fused_ops) {
  // Gets trainable forward ops.
  HashSet<OpNode*> trainable_fw_ops;
  JUST(GetTrainableFwOps(op_graph, &trainable_fw_ops));
  // Gets backbone ops.
  HashSet<OpNode*> backbone_op_nodes;
  JUST(GetBackBoneOps(op_graph, trainable_fw_ops, &backbone_op_nodes));
  // Fuses other forward ops to backbone ops.
  HashMap<OpNode*, OpNode*> other_fw_op2backbone_op;
  JUST(FuseOtherFwOpsToBackboneOps(op_graph, backbone_op_nodes, &other_fw_op2backbone_op));
  for (OpNode* backbone_op_node : backbone_op_nodes) {
    (*backbone_op2fused_ops)[backbone_op_node].reset(new std::vector<OpNode*>{backbone_op_node});
  }
  for (const auto& pair : other_fw_op2backbone_op) {
    (*backbone_op2fused_ops)[pair.second]->push_back(pair.first);
  }
  return Maybe<void>::Ok();
}

// subgraph trainable_fw_ops can be regarded as DAG whose source nodes are variable op nodes and
// whose sink nodes are loss op nodes.
//
// A op node is called backbone op node in trainable_fw_ops if:
//    a) it has two input in subgraph trainable_fw_ops;
//    b) or it has at least one backbone op as input
Maybe<void> GetBackBoneOps(const OpGraph& op_graph, const HashSet<OpNode*>& trainable_fw_ops,
                           HashSet<OpNode*>* backbone_op_nodes) {
  std::list<OpNode*> starts;
  {
    const auto& ForEachIn = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
      node->ForEachNodeOnInEdge([&](OpNode* in_node) {
        if (trainable_fw_ops.count(in_node) > 0) { Handler(in_node); }
      });
    };
    const auto& GetInputSize = [&](OpNode* node) {
      size_t input_size = 0;
      ForEachIn(node, [&](OpNode*) { ++input_size; });
      return input_size;
    };
    for (OpNode* op_node : trainable_fw_ops) {
      if (GetInputSize(op_node) > 1) { starts.push_back(op_node); }
    }
  }
  const auto& ForEachOut = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
    node->ForEachNodeOnOutEdge([&](OpNode* out_node) {
      if (trainable_fw_ops.count(out_node) > 0) { Handler(out_node); }
    });
  };
  op_graph.BfsForEachNode(starts, ForEachOut,
                          [&](OpNode* node) { backbone_op_nodes->insert(node); });
  return Maybe<void>::Ok();
}

Maybe<void> BfsForEachBackboneOp(const OpGraph& op_graph, const HashSet<OpNode*>& backbone_op_nodes,
                                 const std::function<void(OpNode*)>& Handler);

Maybe<void> FuseOtherFwOpsToBackboneOps(const OpGraph& op_graph,
                                        const HashSet<OpNode*>& backbone_op_nodes,
                                        HashMap<OpNode*, OpNode*>* other_fw_op2backbone_op) {
  const auto& ForEachNextOther = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
    node->ForEachNodeOnInEdge([&](OpNode* in_node) {
      if (backbone_op_nodes.count(in_node) > 0) { return; }
      // It's safe to update container other_fw_op2backbone_op when traversing.
      if (other_fw_op2backbone_op->count(in_node) > 0) { return; }
      // Traverses other nodes.
      Handler(in_node);
    });
  };
  const auto& DoEachBackboneOp = [&](OpNode* backbone_op_node) {
    op_graph.BfsForEachNode({backbone_op_node}, ForEachNextOther, [&](OpNode* other) {
      if (backbone_op_nodes.count(other) > 0) { return; }
      (*other_fw_op2backbone_op)[other] = backbone_op_node;
    });
  };
  JUST(BfsForEachBackboneOp(op_graph, backbone_op_nodes, DoEachBackboneOp));
  return Maybe<void>::Ok();
}

Maybe<void> BfsForEachBackboneOp(const OpGraph& op_graph, const HashSet<OpNode*>& backbone_op_nodes,
                                 const std::function<void(OpNode*)>& Handler) {
  std::list<OpNode*> starts;
  {
    const auto& ForEachIn = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
      node->ForEachNodeOnInEdge([&](OpNode* in_node) {
        if (backbone_op_nodes.count(in_node) > 0) { Handler(in_node); }
      });
    };
    const auto& IsSource = [&](OpNode* node) {
      size_t in_size = 0;
      ForEachIn(node, [&](OpNode*) { ++in_size; });
      return in_size == 0;
    };
    for (OpNode* op_node : backbone_op_nodes) {
      if (IsSource(op_node)) { starts.push_back(op_node); }
    }
  }
  const auto& ForEachOut = [&](OpNode* node, const std::function<void(OpNode*)>& Handler) {
    node->ForEachNodeOnOutEdge([&](OpNode* out_node) {
      if (backbone_op_nodes.count(out_node) > 0) { Handler(out_node); }
    });
  };
  op_graph.BfsForEachNode(starts, ForEachOut, Handler);
  return Maybe<void>::Ok();
}

Maybe<void> MakeGetterSspScope4Stage(
    const SspPartitionConf& ssp_partition_conf,
    std::function<Maybe<const Scope&>(int64_t stage)>* SspScope4Stage) {
  const auto& scope_id_conf = ssp_partition_conf.stage_scope_id();
  std::vector<int64_t> stage2scope_id(scope_id_conf.begin(), scope_id_conf.end());
  *SspScope4Stage = [stage2scope_id](int64_t stage) -> Maybe<const Scope&> {
    CHECK_GE_OR_RETURN(stage, 0);
    CHECK_LT_OR_RETURN(stage, stage2scope_id.size());
    int64_t scope_id = stage2scope_id.at(stage);
    return Global<vm::SymbolStorage<Scope>>::Get()->Get(scope_id);
  };
  return Maybe<void>::Ok();
}

}  // namespace

}  // namespace oneflow
