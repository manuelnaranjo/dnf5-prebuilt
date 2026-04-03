#!/usr/bin/env bash

set -exou pipefail

pwd > /dev/stderr

./tools/dnf5lock \
    --output=first.json \
    --arch=x86_64 \
    --config=$(pwd)/centos/stream-9/etc/dnf/dnf.conf \
    --repodir=$(pwd)/centos/stream-9/etc/yum.repos.d \
    --var=basearch=x86_64 \
    --var=releasever=9 \
    --var=releasever_major=9 \
    --var=releasever_minor= \
    --var=stream=9-stream \
    bash

for i in {1..10}; do
    ./tools/dnf5lock \
        --output=second.json \
        --arch=x86_64 \
        --config=$(pwd)/centos/stream-9/etc/dnf/dnf.conf \
        --repodir=$(pwd)/centos/stream-9/etc/yum.repos.d \
        --var=basearch=x86_64 \
        --var=releasever=9 \
        --var=releasever_major=9 \
        --var=releasever_minor= \
        --var=stream=9-stream \
        bash

    diff first.json second.json
done

rm first.json second.json
