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

#ifndef MXNET_NPZ_H_
#define MXNET_NPZ_H_

#include <mxnet/ndarray.h>
#include <string>

namespace mxnet {

namespace npy {

void save_array(const std::string& fname, const NDArray& array);
NDArray load_array(const std::string& fname);

}

namespace npz {

void save_array(int write_mode, const std::string& zip_fname, const std::string& array_name,
                const NDArray& array);

std::pair<std::vector<NDArray>, std::vector<std::string>>  load_arrays(const std::string& fname);

}
}  // namespace mxnet
#endif
