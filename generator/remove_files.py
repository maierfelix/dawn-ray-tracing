#!/usr/bin/env python3
# Copyright 2019 The Dawn Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse, glob, os, sys

def check_in_subdirectory(path, directory):
    return path.startswith(directory) and not '/' in path[len(directory):]

def check_is_allowed(path, allowed_dirs):
    return any(check_in_subdirectory(path, directory) for directory in allowed_dirs)

def get_all_files_in_dir(find_directory):
    result = []
    for (directory, _, files) in os.walk(find_directory):
        result += [os.path.join(directory, filename) for filename in files]
    return result

def run():
    # Parse command line arguments
    parser = argparse.ArgumentParser(
        description = "Removes stale autogenerated files from gen/ directories."
    )
    parser.add_argument('--root-dir', type=str, help='The root directory, all other paths in files are relative to it.')
    parser.add_argument('--allowed-output-dirs-file', type=str, help='The file containing a list of allowed directories')
    parser.add_argument('--stale-dirs-file', type=str, help='The file containing a list of directories to check for stale files')
    parser.add_argument('--stamp', type=str, help='A stamp written once this script completes')
    args = parser.parse_args()

    root_dir = args.root_dir
    stamp_file = args.stamp

    # Load the list of allowed and stale directories
    with open(args.allowed_output_dirs_file) as f:
        allowed_dirs = set([os.path.join(root_dir, line.strip()) for line in f.readlines()])

    for directory in allowed_dirs:
        if not directory.endswith('/'):
            print('Allowed directory entry "{}" doesn\'t end with /'.format(directory))
            return 1

    with open(args.stale_dirs_file) as f:
        stale_dirs = set([line.strip() for line in f.readlines()])

    # Remove all files in stale dirs that aren't in the allowed dirs.
    for stale_dir in stale_dirs:
        stale_dir = os.path.join(root_dir, stale_dir)

        for candidate in get_all_files_in_dir(stale_dir):
            if not check_is_allowed(candidate, allowed_dirs):
                os.remove(candidate)

    # Finished! Write the stamp file so ninja knows to not run this again.
    with open(stamp_file, "w") as f:
        f.write("")

    return 0

if __name__ == "__main__":
    sys.exit(run())
