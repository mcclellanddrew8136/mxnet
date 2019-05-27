/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * Copyright (c) 2019 by Contributors
 * \file simple_partition_pass.h
 * \brief 
 * \author Clement Fuji Tsang
 */
#ifndef MXNET_EXECUTOR_SIMPLE_PARTITION_PASS_H_
#define MXNET_EXECUTOR_SIMPLE_PARTITION_PASS_H_

#include <mxnet/base.h>
#include <mxnet/op_attr_types.h>
#include <mxnet/operator.h>
#include <nnvm/graph_attr_types.h>

#include "exec_pass.h"

namespace mxnet {
namespace exec {


/*!
 * \brief Custom graph class, which will contain bi-directional nodes
 * we need to compute DFS and reverse DFS for graph partitioning
 */
class BidirectionalGraph {
 public:
  struct Node {
    nnvm::Node* nnvmptr;
    std::vector<Node*> inputs;
    std::vector<Node*> outputs;
  };

  explicit BidirectionalGraph(const Graph &g) {
    auto& idx = g.indexed_graph();
    auto num_nodes = idx.num_nodes();
    nodes.reserve(num_nodes);
    nnvm2nid.reserve(num_nodes);
    outputs.reserve(idx.outputs().size());
    DFSVisit(g.outputs, [this](const nnvm::NodePtr& n) {
      Node new_node;
      new_node.nnvmptr = n.get();
      nnvm2nid[n.get()] = static_cast<uint32_t>(nodes.size());
      nodes.emplace_back(std::move(new_node));
    });
    for (const auto& it : nnvm2nid) {
      nnvm::Node* nnvmnode = it.first;
      uint32_t nid = it.second;
      for (auto& n : nnvmnode->inputs) {
        uint32_t input_nid = nnvm2nid[n.node.get()];
        nodes[input_nid].outputs.emplace_back(&nodes[nid]);
        nodes[nid].inputs.emplace_back(&nodes[input_nid]);
      }
    }
    for (auto& e : g.outputs) {
      uint32_t nid = nnvm2nid[e.node.get()];
      outputs.emplace_back(&nodes[nid]);
    }
  }

  template<typename FCompatible>
  std::vector<std::unordered_set<Node*>> get_subsets(FCompatible is_compatible) {
    std::vector<std::unordered_set<Node*>> subgraphs;
    std::unordered_set<Node*> incomp_set;
    std::unordered_set<Node*> all_set(nodes.size());
    std::vector<PairSet> separation_sets;
    for (Node& node : nodes) {
      std::cout << "Looking at " << node.nnvmptr->attrs.name << std::endl;
      if (!is_compatible(node.nnvmptr)) {
        incomp_set.insert(&node);
        std::unordered_set<Node*> in_graph;
        std::unordered_set<Node*> out_graph;
        std::vector<Node*> dummy_head;
        dummy_head.emplace_back(&node);
        DFS(dummy_head, false, [&out_graph](Node* node) {
          out_graph.insert(node);
        });
        DFS(dummy_head, true, [&in_graph](Node* node) {
          in_graph.insert(node);
        });
        if (!(in_graph.empty() || out_graph.empty()))
          separation_sets.push_back(std::make_pair(in_graph, out_graph));
      }
      all_set.emplace(&node);
    }
    IncompMap incomp_map;
    std::unordered_set<Node*> comp_set;
    comp_set.insert(all_set.begin(), all_set.end());
    for (Node* n : incomp_set) {
      comp_set.erase(n);
    }
    for (Node* n : comp_set) {
      for (PairSet p : separation_sets) {
        if (p.first.count(n)) {
          incomp_map[n].insert(p.second.begin(), p.second.end());
        } else if (p.second.count(n)) {
          incomp_map[n].insert(p.first.begin(), p.first.end());
        }
      }
      for (Node* incomp_n : incomp_set) {
        incomp_map[n].erase(incomp_n);
      }
    }
    std::unordered_set<Node*> unused_set;
    unused_set.reserve(comp_set.size());

    for (auto& n : comp_set) {
      unused_set.insert(n);
    }
    std::unordered_set<Node*> visited;
    std::deque<Node*> stack(outputs.begin(), outputs.end());
    while (!stack.empty()) {
      Node* vertex = stack.front();
      std::cout << "Checking " << vertex->nnvmptr->attrs.name << std::endl;
      stack.pop_front();
      if (!visited.count(vertex)) {
        std::cout << "Not visited!" << std::endl;
        visited.insert(vertex);
        if (unused_set.count(vertex)) {
          std::cout << "Adding to subgraphs!" << std::endl;
          subgraphs.emplace_back(naive_grow_subgraph(vertex, &unused_set, &incomp_map));
        }
        for (Node* input : vertex->inputs) {
          stack.emplace_back(input);
        }
      }
    }
    return subgraphs;
  }

 private:
  using PairSet = std::pair<std::unordered_set<Node*>, std::unordered_set<Node*>>;
  using PairVec = std::pair<std::vector<Node*>, std::vector<Node*>>;
  using IncompMap = std::unordered_map<Node*, std::unordered_set<Node*>>;

 template <typename FVisit>
  void DFS(const std::vector<Node*>& heads, bool reverse, FVisit fvisit) {
    std::unordered_set<Node*> visited;
    std::vector<Node*> vec(heads.begin(), heads.end());
    visited.reserve(heads.size());
    while (!vec.empty()) {
      Node* vertex = vec.back();
      vec.pop_back();
      if (visited.count(vertex) == 0) {
        visited.insert(vertex);
        fvisit(vertex);
        std::vector<Node*> nexts = reverse ? vertex->inputs : vertex->outputs;
        for (Node* node : nexts) {
          if (visited.count(node) == 0) {
            vec.emplace_back(node);
          }
        }
      }
    }
  }

  std::unordered_set<Node*> naive_grow_subgraph(Node* head,
                                                std::unordered_set<Node*>* unused_set,
                                                IncompMap* incomp_map) {
    std::unordered_set<Node*> subgraph;
    std::unordered_set<Node*> incomp_set;
    std::deque<Node*> stack;
    stack.emplace_back(head);
    std::cout << "naive grow subgraph" << std::endl;
    while (!stack.empty()) {
      Node* vertex = stack.back();
      std::cout << "Naive sees " << vertex->nnvmptr->attrs.name << std::endl;
      stack.pop_back();
      std::cout << "Unused: " << unused_set->count(vertex) << std::endl;
      std::cout << "Compatible: " << !incomp_set.count(vertex) << std::endl;
      if (unused_set->count(vertex) && !incomp_set.count(vertex)) {
        unused_set->erase(vertex);
        std::cout << "Put into subgraph!" << std::endl;
        subgraph.insert(vertex);
        incomp_set.insert((*incomp_map)[vertex].begin(), (*incomp_map)[vertex].end());
        for (Node* input : vertex->inputs) {
          if (unused_set->count(input) && !incomp_set.count(input)) {
            stack.emplace_back(input);
          }
        }
        for (Node* output : vertex->outputs) {
          if (unused_set->count(output) && !incomp_set.count(output)) {
            stack.emplace_back(output);
          }
        }
      }
    }
    return subgraph;
  }

  friend class Graph;

  std::vector<Node> nodes;
  std::unordered_map<nnvm::Node*, uint32_t> nnvm2nid;
  std::vector<Node*> outputs;
};  // class BidirectionalGraph

using NodeEntrySet = std::unordered_set<nnvm::NodeEntry, nnvm::NodeEntryHash,
                                        nnvm::NodeEntryEqual>;
using NodeRawPtrSet = std::unordered_set<nnvm::Node*>;

/*!
 * \brief get the output nodes of the subgraph in the main graph
 * \return a map between the node in the main graph and the output index of the subgraph node
*/
nnvm::NodeEntryMap<uint32_t> GetSubgraphOutputs(Graph g, NodeRawPtrSet subgraph_set) {
  //std::vector<nnvm::NodeEntry> outputs;
  //NodeEntrySet _outputs;
  nnvm::NodeEntryMap<uint32_t> outputs;
  uint32_t count = 0;
  for (auto& e : g.outputs) {
    if (subgraph_set.count(e.node.get()) && !outputs.count(e)) {
      outputs.insert({e, count++});
    }
  }
  DFSVisit(g.outputs, [&subgraph_set, &outputs, &count](const nnvm::NodePtr &node){
    if (!subgraph_set.count(node.get())) {
      for (auto& e : node->inputs) {
        if (subgraph_set.count(e.node.get()) && !outputs.count(e)) {
          outputs.insert({e, count++});
        }
      }
    }
  });
  //outputs.insert(outputs.begin(), _outputs.begin(), _outputs.end());
  return outputs;
}

/*!
 * \brief create new input nodes of the subgraph and plug them
 * \return the inputs of the subgraph node in the main graph
*/
std::vector<nnvm::NodeEntry> GetSubgraphInputs(Graph g, NodeRawPtrSet subgraph_set) {
  std::vector<nnvm::NodeEntry> inputs;
  const auto &idx = g.indexed_graph();
  nnvm::NodeEntryMap<nnvm::NodeEntry> entry_map;
  DFSVisit(g.outputs, [&subgraph_set, &inputs, &entry_map](const nnvm::NodePtr &node){
    if (subgraph_set.count(node.get())) {
      for (auto &e : node->inputs) {
        if (!subgraph_set.count(e.node.get())) {
          if (entry_map.count(e)) {
            e = entry_map[e];
          } else {
            auto new_node = nnvm::Node::Create();
            new_node->attrs.name = e.node->attrs.name + std::to_string(e.index);
            entry_map.insert({e, nnvm::NodeEntry{new_node, 0, 0}});
            inputs.push_back(e);
            e.node = new_node;
            e.index = 0;
          }
        }
      }
    }
  });
  // Fix ordering of w.r.t to topology
  std::sort(inputs.begin(), inputs.end(),
      [&idx](const nnvm::NodeEntry lhs, const nnvm::NodeEntry rhs) {
        return idx.entry_id(lhs) < idx.entry_id(rhs);
      });
  return inputs;
}

std::unordered_map<uint32_t, uint32_t> GetGraphInputsMap(const Graph& g) {
  std::unordered_map<uint32_t, uint32_t> outputs;
  auto& idx = g.indexed_graph();
  outputs.reserve(idx.num_nodes());
  std::vector<uint32_t> input_nodes = idx.input_nodes();
  for (size_t i = 0; i < input_nodes.size(); ++i) {
    outputs[input_nodes[i]] = static_cast<uint32_t>(i);
  }
  return outputs;
}

/*!
 * \brief helper function to display what nodes are in a specific subset
 */
void dispNodesSet(Graph g, NodeRawPtrSet s) {
  DFSVisit(g.outputs, [&s](const nnvm::NodePtr n){
    if (s.count(n.get())) {
      std::cout << "  Y " << n->attrs.name << std::endl;
    } else {
      std::cout << "  N " << n->attrs.name << std::endl;
    }
  });
}

/*!
 * \brief Replace a set of nodes by a subgraph node
 */
template<typename FCreateNode>
Graph ReplaceSubgraphs(Graph&& g, const std::vector<NodeRawPtrSet>& subgraph_sets,
                       FCreateNode create_subgraph_node) {
  for (auto subgraph_set : subgraph_sets) {
    // Create MXNet subgraph
    Graph subgraph;
    const auto sub_outputs_in_main = GetSubgraphOutputs(g, subgraph_set);
    subgraph.outputs.resize(sub_outputs_in_main.size());
    for (auto p : sub_outputs_in_main) {
      subgraph.outputs[p.second] = p.first;
    }
    // To generate a subgraph an input have to be replace by data node (no op)
    // and it have to be agnostic to the node from which it's an output
    // (For exemple even if two inputs are two different outputs from the same node)
    auto inputs = GetSubgraphInputs(subgraph, subgraph_set);
    auto subgraph_node = create_subgraph_node(subgraph);
    subgraph_node->inputs = inputs;
    // replug inputs of node out of subgraph to be output of the subgraph node
    // if it was a node in the subgraph
    DFSVisit(g.outputs,
        [&subgraph_node, &subgraph_set, &sub_outputs_in_main](const nnvm::NodePtr node) {
      if (!subgraph_set.count(node.get())) {
        for (auto &e : node->inputs) {
          auto it = sub_outputs_in_main.find(e);
          if (it != sub_outputs_in_main.end()) {
            e.node = subgraph_node;
            e.index = it->second;
          }
        }
      }
    });
    // replug outputs of the graph to be output of the subgraph node
    // if it was a node in the subgraph
    for (auto &e : g.outputs) {
      auto it = sub_outputs_in_main.find(e);
      if (it != sub_outputs_in_main.end()) {
        e.node = subgraph_node;
        e.index = it->second;
      }
    }
    // move control dependencies between nodes of the subgraph and out of the subgraph
    // to a dependencies between the subgraph node and the nodes out of the subgraph
    DFSVisit(g.outputs, [&subgraph_node, &subgraph_set](const nnvm::NodePtr& node) {
      for (auto &e : node->control_deps) {
        if (subgraph_set.count(e.get()))
	  e = subgraph_node;
      }
    });
    DFSVisit(subgraph.outputs, [&subgraph_node, &subgraph_set](const nnvm::NodePtr& node) {
      auto it = node->control_deps.begin();
      while (it != node->control_deps.end()) {
        if (subgraph_set.count(it->get())) {
          ++it;
        } else {
          subgraph_node->control_deps.push_back(*it);
          it = node->control_deps.erase(it);
        }
      }
    });
  }
  Graph new_graph;
  new_graph.outputs = g.outputs;
  return new_graph;
}

template<typename FCompatible>
std::vector<NodeRawPtrSet> GetCompatibleSubsets(const Graph& g, FCompatible is_compatible) {
  BidirectionalGraph biG = BidirectionalGraph(g);
  std::vector<std::unordered_set<BidirectionalGraph::Node*>> subsets = biG.get_subsets(is_compatible);
  std::vector<NodeRawPtrSet> nnvm_subsets;
  nnvm_subsets.reserve(subsets.size());
  for (auto& subset : subsets) {
    if (subset.size() > 1) {
      NodeRawPtrSet node_set;
      node_set.reserve(subset.size());
      for (auto& n : subset) {
        node_set.insert(n->nnvmptr);
      }
      nnvm_subsets.push_back(node_set);
    }
  }
  return nnvm_subsets;
}

}  // namespace exec
}  // namespace mxnet
#endif  // MXNET_EXECUTOR_SIMPLE_PARTITION_PASS_H_
