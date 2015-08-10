/*!
 * Copyright (c) 2015 by Contributors
 * \file composite_operator.h
 * \brief composite operator of mxnet
 * \author Bing Xu
*/
#ifndef MXNET_OPERATOR_COMPOSITE_OPERATOR_H_
#define MXNET_OPERATOR_COMPOSITE_OPERATOR_H_
#include <string>
#include <vector>
#include <unordered_map>
#include "./atomic_symbol.h"
#include "./base.h"
#include "./static_graph.h"
#include "./static_operator.h"

namespace mxnet {
/*!
 * \brief composite_operator interface
 * composite operator is a combination of static operator from static graph
 */
class CompositeOperator : public Operator {
 public:
  /*! \brief destructor */
  virtual ~CompositeOperator() {}
  /*!
   * \brief describe property of op
   * \return a bit map in int
   */
  virtual int DescribeProperty() const {
    // default most of layer only conatin internal state
    return kContainInteralState;
  }
  /*! \brief Make operator by using graph
   *  \param ctx ctx context of the created operator
   *  \param in input narray
   *  \param graph input static graph
   */
  void Bind(Context ctx,
            const std::vector<NArray> &in,
            std::shared_ptr<StaticGraph> graph);
  /*!
   * \brief perform a forward operation of operator, save the output to NArray
   *        This method only pushes an execution request to the DAG engine, and
   *        return immediately. Actual execution is conducted by the DAG engine.
   * \param opt option on Forward such as whether this is training phase
   * \param ctx runtime context
   * \param in_data array of input data, it is const
   * \param out_data array of output data,
   *        the space of NArray in out_data must be pre-allocated with InferShape
   * \sa NArray
   */
  virtual void Forward(Option opt,
                       RunContext ctx,
                       const std::vector<NArray> &in_data,
                       const std::vector<NArray> &out_data);
  /*!
   * \brief perform a backward operation of the operator to get the gradient
   *        This method only pushes an execution request to the DAG engine, and
   *        return immediately. Actual execution is conducted by the DAG engine.
   * \param ctx runtime context
   * \param grad_next the gradient value of the output of the operator, used by chain rule.
   * \param in_data the array of input data
   * \param out_grad array of output gradient
   * \param req request types of the gradient saving operation
   *                  only inplace will change input data
   * \sa GradReqType, NArray
   */
  virtual void Backward(RunContext ctx,
                        const std::vector<NArray> &grad_next,
                        const std::vector<NArray> &in_data,
                        const std::vector<NArray> &out_grad,
                        const std::vector<GradReqType> &req);
  /*!
   * \brief perform an extraction operation to get feature map 
   * \param name of symbol need to be extracted
   * \return empty narray for invalid name or narray of the feature map
   */
  virtual NArray Extract(const std::string &symbol_name);

 private:
  /*! \brief 
  struct Connection {

  };
  /*! \brief static operators for each node */
  std::vector<unique_ptr<Operator> > static_ops_;
  /*! \brief feature map for each op */
  std::vector<std::vector<NArray> > feature_maps_;
  /*! \brief input NArray link */
  std::vector<std::vector<NArray> > in_data_;
  /*! \brief input NArray gradient */
  std::vector<std::vector<NArray> > in_grad_;
  /*! \brief output NArray link */
  std::vector<std::vector<NArray> > out_data_;
  /*! \brief static graph */
  std::shared_ptr<StaticGraph> graph_;
};  // class CompositeOperator
}  // namespace mxnet
#endif  // MXNET_OPERATOR_COMPOSITE_OPERATOR_H_


