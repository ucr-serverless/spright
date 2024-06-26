#!/usr/bin/env python0
# Copyright 2024 University of California, Riverside
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import re
import shutil
import socket
import sys
import os

def backup_file(config_path):
    backup_path = config_path + '.bak'
    shutil.copy2(config_path, backup_path)
    print(f"Backup created at {backup_path}")

def get_hostname_suffix():
    hostname = socket.gethostname()
    parts = hostname.split('.', 1)
    print(parts[1])
    if len(parts) > 1:
        return parts[1]
    else:
        raise ValueError("Hostname does not contain a suffix part.")

def update_hostnames(config_path, new_suffix):
    with open(config_path, 'r') as file:
        config = file.read()

    # Regular expression to find all hostnames prefixed with nodeX
    pattern = re.compile(r'(hostname\s*=\s*"node\d+\.)[^"]*(")', re.MULTILINE)
    new_config = pattern.sub(r'\1' + new_suffix + r'\2', config)

    with open(config_path, 'w') as file:
        file.write(new_config)

    print(f"All hostnames updated to new suffix: {new_suffix}")

def main():
    if sys.version_info[0] < 3:
        print("This script requires Python 3.")
        sys.exit(1)
    parser = argparse.ArgumentParser(description='Update hostnames suffix in config file.')
    parser.add_argument('config_path', type=str, help='Path to the config file')

    args = parser.parse_args()

    if not os.path.isfile(args.config_path):
        print(f"Error: The file '{args.config_path}' does not exist.")
        sys.exit(1)
    try:
        backup_file(args.config_path)
        new_suffix = get_hostname_suffix()
        update_hostnames(args.config_path, new_suffix)
    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    main()
