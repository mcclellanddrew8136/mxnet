;;
;; Licensed to the Apache Software Foundation (ASF) under one or more
;; contributor license agreements.  See the NOTICE file distributed with
;; this work for additional information regarding copyright ownership.
;; The ASF licenses this file to You under the Apache License, Version 2.0
;; (the "License"); you may not use this file except in compliance with
;; the License.  You may obtain a copy of the License at
;;
;;    http://www.apache.org/licenses/LICENSE-2.0
;;
;; Unless required by applicable law or agreed to in writing, software
;; distributed under the License is distributed on an "AS IS" BASIS,
;; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
;; See the License for the specific language governing permissions and
;; limitations under the License.
;;

(ns tutorial.module
  "A REPL tutorial of the MXNet Clojure API for Module, based on
  https://mxnet.apache.org/api/clojure/module.html"
  (:require [clojure.java.io :as io]
            [clojure.java.shell :refer [sh]]
            [org.apache.clojure-mxnet.eval-metric :as eval-metric]
            [org.apache.clojure-mxnet.io :as mx-io]
            [org.apache.clojure-mxnet.module :as m]
            [org.apache.clojure-mxnet.symbol :as sym]
            [org.apache.clojure-mxnet.ndarray :as ndarray]))


;; The Module API provides an intermediate and high-level interface
;; for performing computation with neural networks in MXNet. Module
;; wraps a Symbol and one or more Executors. It has both a high level
;; and intermediate level API.


;;;; Prepare the Data

;; In this example, we are going to use the MNIST data set. If you
;; start, we can run some helper scripts to download the data for us.

(def data-dir "data/")

(when-not (.exists (io/file (str data-dir "train-images-idx3-ubyte")))
  (sh "../../scripts/get_mnist_data.sh"))

;; MXNet provides function in the `io` namespace to load the MNIST
;; datasets into training and test data iterators that we can use with
;; our module.
(def train-data (mx-io/mnist-iter {:image (str data-dir "train-images-idx3-ubyte")
                                   :label (str data-dir "train-labels-idx1-ubyte")
                                   :label-name "softmax_label"
                                   :input-shape [784]
                                   :batch-size 10
                                   :shuffle true
                                   :flat true
                                   :silent false
                                   :seed 10}))

(def test-data (mx-io/mnist-iter {:image (str data-dir "t10k-images-idx3-ubyte")
                                  :label (str data-dir "t10k-labels-idx1-ubyte")
                                  :input-shape [784]
                                  :batch-size 10
                                  :flat true
                                  :silent false}))


;;;; Preparing a module for Computation

;; To construct a module, we need to have a symbol as input. This
;; symbol takes input data in the first layer and then has subsequent
;; layers of fully connected and relu activation layers, ending up in
;; a softmax layer for output.

(let [data (sym/variable "data")
      fc1 (sym/fully-connected "fc1" {:data data :num-hidden 128})
      act1 (sym/activation "relu1" {:data fc1 :act-type "relu"})
      fc2 (sym/fully-connected "fc2" {:data act1 :num-hidden 64})
      act2 (sym/activation "relu2" {:data fc2 :act-type "relu"})
      fc3 (sym/fully-connected "fc3" {:data act2 :num-hidden 10})
      out (sym/softmax-output "softmax" {:data fc3})]
  out) ;=>#object[org.apache.mxnet.Symbol 0x1f43a406 "org.apache.mxnet.Symbol@1f43a406"]

;; You can also write this with the `as->` threading macro.


(def out (as-> (sym/variable "data") data
           (sym/fully-connected "fc1" {:data data :num-hidden 128})
           (sym/activation "relu1" {:data data :act-type "relu"})
           (sym/fully-connected "fc2" {:data data :num-hidden 64})
           (sym/activation "relu2" {:data data :act-type "relu"})
           (sym/fully-connected "fc3" {:data data :num-hidden 10})
           (sym/softmax-output "softmax" {:data data})))
;=> #'tutorial.module/out


;; By default, context is the CPU. If you need data parallelization,
;; you can specify a GPU context or an array of GPU contexts, like
;; this: `(m/module out {:contexts [(context/gpu)]})`

;; Before you can compute with a module, you need to call `bind` to
;; allocate the device memory and `initParams` or `set-params` to
;; initialize the parameters. If you simply want to fit a module, you
;; don’t need to call `bind` and `init-params` explicitly, because the
;; `fit` function automatically calls them if they are needed.

(let [mod (m/module out)]
  (-> mod
      (m/bind {:data-shapes (mx-io/provide-data train-data)
               :label-shapes (mx-io/provide-label train-data)})
      (m/init-params)))

;; Now you can compute with the module using functions like `forward`,
;; `backward`, etc.


;;;; Training and Predicting

;; Modules provide high-level APIs for training, predicting, and
;; evaluating. To fit a module, call the `fit` function with some data
;; iterators:

(def mod
  (m/fit (m/module out) {:train-data train-data
                         :eval-data test-data
                         :num-epoch 1}))
;; =>
;; Epoch  0  Train- [accuracy 0.12521666]
;; Epoch  0  Time cost- 8392
;; Epoch  0  Validation-  [accuracy 0.2227]


;; You can pass in batch-end callbacks using batch-end-callback and
;; epoch-end callbacks using epoch-end-callback in the
;; `fit-params`. You can also set parameters using functions like in
;; the fit-params like optimizer and eval-metric. To learn more about
;; the fit-params, see the fit-param function options. To predict with
;; a module, call `predict` with a DataIter:

(def results
  (m/predict mod {:eval-data test-data}))

(first results) ;=>#object[org.apache.mxnet.NDArray 0x3540b6d3 "org.apache.mxnet.NDArray@a48686ec"]

(first (ndarray/->vec (first results))) ;=>0.08261358

;; The module collects and returns all of the prediction results. For
;; more details about the format of the return values, see the
;; documentation for the `predict` function.

;; When prediction results might be too large to fit in memory, use
;; the `predict-every-batch` API.

(let [preds (m/predict-every-batch mod {:eval-data test-data})]
  (mx-io/reduce-batches test-data
                        (fn [i batch]
                          (println (str "pred is " (first (get preds i))))
                          (println (str "label is " (mx-io/batch-label batch)))
                          ;;; do something
                          (inc i))))

;; If you need to evaluate on a test set and don’t need the prediction
;; output, call the `score` function with a data iterator and an eval
;; metric:

(m/score mod {:eval-data test-data
              :eval-metric (eval-metric/accuracy)}) ;=>["accuracy" 0.2227]

;; This runs predictions on each batch in the provided DataIter and
;; computes the evaluation score using the provided EvalMetric. The
;; evaluation results are stored in metric so that you can query
;; later.



;;;; Saving and Loading

;; To save the module parameters in each training epoch, use the
;; `save-checkpoint` function:

(let [save-prefix "my-model"]
  (doseq [epoch-num (range 3)]
    (mx-io/do-batches train-data (fn [batch
                                          ;; do something
                                     ]))
    (m/save-checkpoint mod {:prefix save-prefix
                            :epoch epoch-num
                            :save-opt-states true}))) 

;; INFO  org.apache.mxnet.module.Module: Saved checkpoint to my-model-0000.params
;; INFO  org.apache.mxnet.module.Module: Saved optimizer state to my-model-0000.states
;; INFO  org.apache.mxnet.module.Module: Saved checkpoint to my-model-0001.params
;; INFO  org.apache.mxnet.module.Module: Saved optimizer state to my-model-0001.states
;; INFO  org.apache.mxnet.module.Module: Saved checkpoint to my-model-0002.params
;; INFO  org.apache.mxnet.module.Module: Saved optimizer state to my-model-0002.states


;; To load the saved module parameters, call the `load-checkpoint`
;; function:

(def new-mod (m/load-checkpoint {:prefix "my-model" :epoch 1 :load-optimizer-states true}))

new-mod ;=> #object[org.apache.mxnet.module.Module 0x5304d0f4 "org.apache.mxnet.module.Module@5304d0f4"]

;; To initialize parameters, bind the symbols to construct executors
;; first with the `bind` function. Then, initialize the parameters and
;; auxiliary states by calling the `init-params` function.\
(-> new-mod
    (m/bind {:data-shapes (mx-io/provide-data train-data)
             :label-shapes (mx-io/provide-label train-data)})
    (m/init-params))

;; To get current parameters, use `params`
(let [[arg-params aux-params] (m/params new-mod)]
  {:arg-params arg-params
   :aux-params aux-params})

;; {:arg-params
;;  {"fc3_bias"
;;   #object[org.apache.mxnet.NDArray 0x39adc3b0 "org.apache.mxnet.NDArray@49caf426"],
;;   "fc2_weight"
;;   #object[org.apache.mxnet.NDArray 0x25baf623 "org.apache.mxnet.NDArray@a6c8f9ac"],
;;   "fc1_bias"
;;   #object[org.apache.mxnet.NDArray 0x6e089973 "org.apache.mxnet.NDArray@9f91d6eb"],
;;   "fc3_weight"
;;   #object[org.apache.mxnet.NDArray 0x756fd109 "org.apache.mxnet.NDArray@2dd0fe3c"],
;;   "fc2_bias"
;;   #object[org.apache.mxnet.NDArray 0x1dc69c8b "org.apache.mxnet.NDArray@d128f73d"],
;;   "fc1_weight"
;;   #object[org.apache.mxnet.NDArray 0x20abc769 "org.apache.mxnet.NDArray@b8e1c5e8"]},
;;  :aux-params {}}


;; To assign parameter and aux state values, use the `set-params`
;; function:
(m/set-params new-mod {:arg-params (m/arg-params new-mod)
                       :aux-params (m/aux-params new-mod)})


;; To resume training from a saved checkpoint, pass the loaded
;; parameters to the `fit` function. This will prevent `fit` from
;; initializing randomly.

;; (First, reset the training data before calling `fit` or you will
;; get an error)
(mx-io/reset train-data)
(mx-io/reset test-data)

;; Create `fit-params` and then use it to set `begin-epoch` so that
;; `fit` knows to resume from a saved epoch.


(comment 
;; FIXME
; Caused by: java.io.EOFException
;   at java.io.DataInputStream.readInt(DataInputStream.java:392)
;   at java.io.ObjectInputStream$BlockDataInputStream.readInt(ObjectInputStream.java:3182)
;   at java.io.ObjectInputStream.readInt(ObjectInputStream.java:1032)
;   at org.apache.mxnet.Optimizer$$anon$1$$anonfun$deserializeState$1.apply$mcVI$sp(Optimizer.scala:84)
;   at scala.collection.immutable.Range.foreach$mVc$sp(Range.scala:160)
;   at org.apache.mxnet.Optimizer$$anon$1.deserializeState(Optimizer.scala:83)
;   at org.apache.mxnet.module.Module$$anonfun$loadOptimizerStates$3.apply(Module.scala:594)
;   at org.apache.mxnet.module.Module$$anonfun$loadOptimizerStates$3.apply(Module.scala:589)
;   at scala.Option.foreach(Option.scala:257)
;   at org.apache.mxnet.module.Module.loadOptimizerStates(Module.scala:589)
;   at org.apache.mxnet.module.Module$$anonfun$initOptimizer$4.apply(Module.scala:407)
;   at org.apache.mxnet.module.Module$$anonfun$initOptimizer$4.apply(Module.scala:406)
;   at scala.Option.foreach(Option.scala:257)
;   at org.apache.mxnet.module.Module.initOptimizer(Module.scala:406)
;   at org.apache.mxnet.module.BaseModule.fit(BaseModule.scala:407)
;   at sun.reflect.NativeMethodAccessorImpl.invoke0(Native Method)
;   at sun.reflect.NativeMethodAccessorImpl.invoke(NativeMethodAccessorImpl.java:62)
;   at sun.reflect.DelegatingMethodAccessorImpl.invoke(DelegatingMethodAccessorImpl.java:43)
;   at java.lang.reflect.Method.invoke(Method.java:498)
;   at clojure.lang.Reflector.invokeMatchingMethod(Reflector.java:93)
;   at clojure.lang.Reflector.invokeInstanceMethod(Reflector.java:28)
;   at org.apache.clojure_mxnet.module$fit.invokeStatic(module.clj:551)
;   at org.apache.clojure_mxnet.module$fit.invoke(module.clj:538)
;   at tutorial.module$eval1787.invokeStatic(module.clj:250)
;   at tutorial.module$eval1787.invoke(module.clj:250)

(m/fit new-mod {:train-data train-data
                :eval-data test-data
                :num-epoch 2
                :fit-params (m/fit-params {:begin-epoch 1})})

)