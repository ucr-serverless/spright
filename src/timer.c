/*
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
*/

#include "timer.h"

void get_monotonic_time(struct timespec *ts)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
}

long get_time_nano(struct timespec *ts)
{
    return (long)ts->tv_sec * 1e9 + ts->tv_nsec;
}

double get_elapsed_time_sec(struct timespec *before, struct timespec *after)
{
    double deltat_s = after->tv_sec - before->tv_sec;
    double deltat_ns = after->tv_nsec - before->tv_nsec;
    return deltat_s + deltat_ns * 1e-9;
}

long get_elapsed_time_nano(struct timespec *before, struct timespec *after)
{
    return get_time_nano(after) - get_time_nano(before);
}
