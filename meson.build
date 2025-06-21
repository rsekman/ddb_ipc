project('ddb_ipc', 'cpp', 'c',
  default_options : ['warning_level=1', 'cpp_std=c++17'],
  version : '0.2.0'
)

destdir = get_option('libdir') / 'deadbeef'

add_global_arguments('-DLIBDIR="'  + get_option('libdir') + '"', language : 'cpp')

incdir = include_directories('src', 'submodules/cpp-base64')

base64_lib = static_library('libbase64',
  'submodules/cpp-base64/base64.cpp',
  include_directories: 'submodules/cpp-base64',
)

shared_module('ddb_ipc',
  'src/ddb_ipc.cpp',
  'src/argument.cpp',
  'src/commands.cpp',
  'src/message.cpp',
  'src/properties.cpp',
  'src/response.cpp',
  include_directories: incdir,
  install: true,
  install_dir: destdir,
  link_with: base64_lib,
  name_prefix: ''
)
