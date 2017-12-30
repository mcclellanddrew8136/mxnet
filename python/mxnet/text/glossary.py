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

# coding: utf-8

"""Read text files and load embeddings."""
from __future__ import absolute_import
from __future__ import print_function

from .. import ndarray as nd
from .embedding import TextEmbed


class Glossary(TextEmbed):
    """Indexing and embedding for text tokens in a glossary.

    For each indexed token in a glossary, an embedding vector will be associated
    with it. Such embedding vectors can be loaded from externally hosted or
    custom pre-trained text embedding files, such as via instances of
    :func:`~mxnet.text.embedding.TextEmbed`.


    Parameters
    ----------
    counter : collections.Counter or None
        Counts text token frequencies in the text data. Its keys will be indexed
        according to frequency thresholds such as `most_freq_count` and
        `min_freq`.
    embeds : an instance or a list of instances of
        :func:`~mxnet.text.embedding.TextEmbed`
        One or multiple pre-trained text embeddings to load. If it is a list of
        multiple embeddings, these embedding vectors will be concatenated for
        each token.
    most_freq_count : None or int, default None
        The maximum possible number of the most frequent tokens in the keys of
        `counter` that can be indexed. Note that this argument does not count
        any token from `reserved_tokens`. If this argument is None or larger
        than its largest possible value restricted by `counter` and
        `reserved_tokens`, this argument becomes positive infinity.
    min_freq : int, default 1
        The minimum frequency required for a token in the keys of `counter` to
        be indexed.
    unknown_token : str, default '<unk>'
        The string representation for any unknown token. In other words, any
        unknown token will be indexed as the same string representation. This
        string representation cannot be any token to be indexed from the keys of
        `counter` or from `reserved_tokens`.
    reserved_tokens : list of strs or None, default None
        A list of reserved tokens that will always be indexed. It cannot contain
        `unknown_token`, or duplicate reserved tokens.

    """
    def __init__(self, counter, embeds, most_freq_count=None, min_freq=1,
                 unknown_token='<unk>', reserved_tokens=None):

        if not isinstance(embeds, list):
            embeds = [embeds]

        # Index tokens from keys of `counter` and reserved tokens.
        super(Glossary, self).__init__(counter=counter,
                                       most_freq_count=most_freq_count,
                                       min_freq=min_freq,
                                       unknown_token=unknown_token,
                                       reserved_tokens=reserved_tokens)

        # Set _idx_to_vec so that indices of tokens from keys of `counter` are
        # associated with text embedding vectors from `embeds`.
        self.set_idx_to_vec_by_embeds(embeds)

    def set_idx_to_vec_by_embeds(self, embeds):
        """Sets the mapping between token indices and token embedding vectors.


        Parameters
        ----------
        embeds : an instance or a list of instances of
            :func:`~mxnet.text.embedding.TextEmbed`
            One or multiple pre-trained text embeddings to load. If it is a list
            of multiple embeddings, these embedding vectors will be concatenated
            for each token.
        """

        if not isinstance(embeds, list):
            embeds = [embeds]

        # Sanity checks.
        for embed in embeds:
            assert isinstance(embed, TextEmbed), \
                'The parameter `embeds` must be an instance or a list of ' \
                'instances of `mxnet.text.embedding.TextEmbed` ' \
                'whose embedding vectors will be loaded or ' \
                'concatenated-then-loaded to map to the indexed tokens.'

            assert self.unknown_token == embed.unknown_token, \
                'The `unknown_token` of the instances of ' \
                '`mxnet.text.embedding.TextEmbed` must be the same as the' \
                '`unknown_token` this glossary. This is to avoid confusion.'

        self._vec_len = sum(embed.vec_len for embed in embeds)
        self._idx_to_vec = nd.zeros(shape=(len(self), self.vec_len))

        col_start = 0
        # Concatenate all the embedding vectors in embeds.
        for embed in embeds:
            col_end = col_start + embed.vec_len
            self._idx_to_vec[:, col_start:col_end] = embed[self.idx_to_token]
            col_start = col_end
