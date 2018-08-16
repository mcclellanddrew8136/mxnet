# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

7z x -y windows_package.7z
$env:MXNET_LIBRARY_PATH=join-path $pwd.Path windows_package\lib\libmxnet.dll
$env:PYTHONPATH=join-path $pwd.Path windows_package\python
$env:MXNET_STORAGE_FALLBACK_LOG_VERBOSE=0
c:\Anaconda3\envs\py2\Scripts\pip install -r tests\requirements.txt
c:\Anaconda3\envs\py2\python.exe -m nose -v --with-xunit --xunit-file nosetests_unittest.xml tests\python\unittest
if (! $?) { Throw ("Error running unittest") }
c:\Anaconda3\envs\py2\python.exe -m nose -v --with-xunit --xunit-file nosetests_operator.xml tests\python\gpu\test_operator_gpu.py
if (! $?) { Throw ("Error running tests") }
c:\Anaconda3\envs\py2\python.exe -m nose -v --with-xunit --xunit-file nosetests_forward.xml tests\python\gpu\test_forward.py
if (! $?) { Throw ("Error running tests") }
c:\Anaconda3\envs\py2\python.exe -m nose -v tests\python\train
if (! $?) { Throw ("Error running tests") }
