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

# -*- coding: utf-8 -*-
import sys, os, re, subprocess
from recommonmark.parser import CommonMarkParser
from recommonmark.transform import AutoStructify

# -- mock out modules
MOCK_MODULES = ['scipy', 'scipy.sparse', 'sklearn']

# -- General configuration -----------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
needs_sphinx = '1.5.6'

# General information about the project.
project = u'Apache MXNet'
author = u'%s developers' % project
copyright = u'2015-2020, %s' % author
github_doc_root = 'https://github.com/apache/incubator-mxnet/tree/master/docs/'
doc_root = 'https://mxnet.apache.org/'

# add markdown parser
source_parsers = {
    '.md': CommonMarkParser,
}
# Version information.

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom ones
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.doctest',
    'sphinx.ext.intersphinx',
    'sphinx.ext.todo',
    'sphinx.ext.mathjax',
    'sphinx.ext.ifconfig',
    'sphinx.ext.viewcode',
    # 'sphinxcontrib.fulltoc',
    'nbsphinx',
    'IPython.sphinxext.ipython_console_highlighting',
    'sphinx.ext.autosummary',
    'sphinx.ext.napoleon',
    'breathe',
#    'mxdoc'
    'autodocsumm',
]

doctest_global_setup = '''
import mxnet as mx
from mxnet import np, npx
'''

autosummary_generate = True
numpydoc_show_class_members = False

# Disable SSL verification in link check.
tls_verify = False

autodoc_member_order = 'alphabetical'

autodoc_default_flags = ['members', 'show-inheritance']

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# The suffix of source filenames.
source_suffix = ['.rst', '.ipynb', '.md', '.Rmd']

# The encoding of source files.
#source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = 'index'

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# The short X.Y version.

# Version and release are passed from CMake.
#version = None

# The full version, including alpha/beta/rc tags.
#release = version

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
#today = ''
# Else, today_fmt is used as the format for a strftime call.
#today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
exclude_patterns = ['templates',
                    # 'api',
                    'guide/modules/others', 'guide/guide', 'blog']

# The reST default role (used for this markup: `text`) to use for all documents.
#default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
#add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
add_module_names = False

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
#show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = 'sphinx'


# A list of ignored prefixes for module index sorting.
#modindex_common_prefix = []

suppress_warnings = [
   'image.nonlocal_uri',
]

# -- Options for HTML output ---------------------------------------------------

# Add any paths that contain custom themes here, relative to this directory.
html_theme_path = ['../../themes/mx-theme']

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
html_theme = 'mxtheme'

# Theme options are theme-specific and customize the look and feel of a theme
# further.  For a list of options available for each theme, see the
# documentation.
html_theme_options = {
    'primary_color': 'blue',
    'accent_color': 'deep_orange',
    'show_footer': True,
    'relative_url': os.environ.get('SPHINX_RELATIVE_URL', '/')
}


# The name for this set of Sphinx documents.  If None, it defaults to
# "<project> v<release> documentation".
#html_title = None

# A shorter title for the navigation bar.  Default is the same as html_title.
#html_short_title = None

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
html_logo = '../../_static/mxnet_logo.png'

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
html_favicon = '../../_static/mxnet-icon.png'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['../../_static']

html_css_files = [
    'mxnet.css',
]

html_js_files = [
    'autodoc.js'
]

# If not '', a 'Last updated on:' timestamp is inserted at every page bottom,
# using the given strftime format.
#html_last_updated_fmt = '%b %d, %Y'

# If true, SmartyPants will be used to convert quotes and dashes to
# typographically correct entities.
#html_use_smartypants = True

# Custom sidebar templates, maps document names to template names.
html_sidebars = {
  '**': 'relations.html'
}

# Additional templates that should be rendered to pages, maps page names to
# template names.
#html_additional_pages = {}

# If false, no module index is generated.
#html_domain_indices = True

# If false, no index is generated.
#html_use_index = True

# If true, the index is split into individual pages for each letter.
#html_split_index = False

# If true, links to the reST sources are added to the pages.
#html_show_sourcelink = True

# If true, "Created using Sphinx" is shown in the HTML footer. Default is True.
html_show_sphinx = False

# If true, "(C) Copyright ..." is shown in the HTML footer. Default is True.
html_show_copyright = False

# If true, an OpenSearch description file will be output, and all pages will
# contain a <link> tag referring to it.  The value of this option must be the
# base URL from which the finished HTML is served.
#html_use_opensearch = ''

# This is the file name suffix for HTML files (e.g. ".xhtml").
#html_file_suffix = None

# Output file base name for HTML help builder.
htmlhelp_basename = 'formatdoc'

nbsphinx_execute = 'never'

# let the source file format to be xxx.ipynb instead of xxx.ipynb.txt
html_sourcelink_suffix = ''

html_context = {
    'display_github': True,
    'github_user': 'apache',
    'github_repo': 'mxnet',
    'github_version': 'master',
    'conf_py_path': '/docs/python_docs/python/',
    'last_updated': False,
    'commit': True
}

def setup(app):
    app.add_transform(AutoStructify)
    app.add_config_value('recommonmark_config', {
    }, True)
    app.add_javascript('matomo_analytics.js')
    import mxtheme
    app.add_directive('card', mxtheme.CardDirective)
