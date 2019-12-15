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

#ifndef MXNET_OPERATOR_SUBGRAPH_PARTITIONER_CUSTOM_SUBGRAPH_PROPERTY_H_
#define MXNET_OPERATOR_SUBGRAPH_PARTITIONER_CUSTOM_SUBGRAPH_PROPERTY_H_

#include <nnvm/pass_functions.h>
#include <string>
#include <utility>
#include <vector>
#include "../common.h"
#include "../subgraph_property.h"
#include "../../include/mxnet/lib_api.h"
namespace mxnet {
namespace op {

/*
 * This selects nodes for a subgraph based on node name as supplied
 * by the supportedOps from an external library. It visits nodes via
 * both input and output links.
 */
class CustomContainOpSelector: public SubgraphSelector {
 public:
  explicit CustomContainOpSelector(std::vector<std::string> supportedNodes) :
    supportedNodes_(supportedNodes) {}
  virtual bool Select(const nnvm::Node &n) {
    return std::find(supportedNodes_.begin(), supportedNodes_.end(),
                     n.attrs.name) != supportedNodes_.end();
  }
  virtual bool SelectInput(const nnvm::Node &n, const nnvm::Node &new_node) {
    return std::find(supportedNodes_.begin(), supportedNodes_.end(),
                     new_node.attrs.name) != supportedNodes_.end();
  }
  virtual bool SelectOutput(const nnvm::Node &n, const nnvm::Node &new_node) {
    return std::find(supportedNodes_.begin(), supportedNodes_.end(),
                     new_node.attrs.name) != supportedNodes_.end();
  }
  std::vector<std::string> supportedNodes_;
};

/*
 * This subgraph property finds a subgraph that only contains
 * nodes as specified by the supportedOps from an external library.
 * The operators in the subgraph will be executed by the operator
 * specified by the external library too.
 */
class  CustomSubgraphProperty: public SubgraphProperty {
 public:
  CustomSubgraphProperty() :
    callSupportedOps_(nullptr),
    supportedOps_(nullptr),
    subgraph_op_name("error"),
    subgraphProp("error") {}
  CustomSubgraphProperty(std::string subgraphProp_name,
                        partCallSupportedOps_t callSupportedOps,
                        supportedOps_t supportedOps,
                        std::string op_name) :
    callSupportedOps_(callSupportedOps),
    supportedOps_(supportedOps),
      subgraph_op_name(op_name),
      subgraphProp(subgraphProp_name) {}

  // create custom subgraph property
  static SubgraphPropertyPtr Create() {
    return std::make_shared<CustomSubgraphProperty>();
  }

  void PrePartition(const nnvm::Graph& g,
    const std::vector<std::pair<std::string, std::string>>& options_map) {
    std::cout << "PrePartition" << std::endl;

    // remove all graph attrs, some cannot be saved to json
    nnvm::Graph graph = std::move(g);
    graph.attrs.clear();
    const nnvm::IndexedGraph& indexed_graph = graph.indexed_graph();

    // set shape attrs for each node in the graph
    if (g.HasAttr("shape")) {
      mxnet::ShapeVector shapes = g.GetAttr<mxnet::ShapeVector>("shape");
      for (unsigned i = 0; i < indexed_graph.num_nodes(); i++) {
        nnvm::Node* node = const_cast<nnvm::Node*>(indexed_graph[i].source);
        mxnet::TShape shape = shapes[i];
        std::stringstream ss;
        ss << shape;
        node->attrs.dict["shape"] = ss.str();
      }
    }
    // set dtype attrs for each node in the graph
    if (g.HasAttr("dtype")) {
      std::vector<int> dtypes = g.GetAttr<std::vector<int> >("dtype");
      for (unsigned i = 0; i < indexed_graph.num_nodes(); i++) {
        nnvm::Node* node = const_cast<nnvm::Node*>(indexed_graph[i].source);
        int dtype = dtypes[i];
        std::stringstream ss;
        ss << dtype;
        node->attrs.dict["dtype"] = ss.str();
      }
    }

    CHECK(supportedOps_ != nullptr)
      << "supportedOps_ is null for " << subgraphProp << std::endl;
    CHECK(callSupportedOps_ != nullptr)
      << "callSupportedOps_ is null for " << subgraphProp << std::endl;

    std::string subgraph_json = nnvm::pass::SaveJSON(graph);
    std::vector<int> supportedNodeIDs(indexed_graph.num_nodes(), 0);
    const char* json = subgraph_json.c_str();
    int *ids = supportedNodeIDs.data();

    CHECK(callSupportedOps_(supportedOps_, json, supportedNodeIDs.size(), ids))
      << "Error calling supportedOps for '" << subgraphProp << "'";

    const auto& idx = g.indexed_graph();
    // loop and add node names for each supported node ID
    for (unsigned i = 0; i < supportedNodeIDs.size(); i++) {
      if (supportedNodeIDs[i]) {
        supportedNodes.push_back(idx[i].source->attrs.name);
      }
    }
  }
  // override CreateSubgraphNode
  virtual nnvm::NodePtr CreateSubgraphNode(const nnvm::Symbol &sym,
                                           const int subgraph_id = 0) const {
    nnvm::NodePtr n = nnvm::Node::Create();
    n->attrs.op = Op::Get(subgraph_op_name);
    n->attrs.name = "_op" + std::to_string(subgraph_id);
    n->attrs.subgraphs.push_back(std::make_shared<nnvm::Symbol>(sym));
    return n;
  }
  // override CreateSubgraphSelector
  virtual SubgraphSelectorPtr CreateSubgraphSelector() const {
    return std::make_shared<CustomContainOpSelector>(supportedNodes);
  }

  partCallSupportedOps_t callSupportedOps_;
  supportedOps_t supportedOps_;
  std::vector<std::string> supportedNodes;
  std::string subgraph_op_name;
  std::string subgraphProp;
};
}  // namespace op
}  // namespace mxnet

#endif  // MXNET_OPERATOR_SUBGRAPH_PARTITIONER_CUSTOM_SUBGRAPH_PROPERTY_H_
