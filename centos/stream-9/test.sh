#!/usr/bin/env bash

set -exou pipefail

./tools/dnf5lock \
    --output=test.json \
    --arch=x86_64 \
    --config=$(pwd)/centos/stream-9/etc/dnf/dnf.conf \
    --repodir=$(pwd)/centos/stream-9/etc/yum.repos.d \
    --var=basearch=x86_64 \
    --var=releasever=9 \
    --var=releasever_major=9 \
    --var=releasever_minor= \
    --var=stream=9-stream \
    core

cat test.json | jq -r
