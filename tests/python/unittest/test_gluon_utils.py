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

import os
import tempfile

import mxnet as mx
from nose.tools import *


@raises(Exception)
def test_download_retries():
    mx.gluon.utils.download("http://doesnotexist.notfound")

def test_download_successful():
    tmp = tempfile.mkdtemp()
    tmpfile = os.path.join(tmp, 'README.md')
    mx.gluon.utils.download("https://raw.githubusercontent.com/apache/incubator-mxnet/master/README.md",
                            path=tmpfile)
    assert os.path.getsize(tmpfile) > 100

def test_download_ssl_verify():
    with warnings.catch_warnings(record=True) as warnings_:
        mx.gluon.utils.download(
            "https://mxnet.incubator.apache.org/", verify_ssl=False)
        assert any(
            str(w.message[0]).startswith('Unverified HTTPS request')
            for w in warnings_)
