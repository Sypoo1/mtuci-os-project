#!/bin/bash

for i in {1..120}; do
    # echo "Running iteration $i"
    python3 client.py test.txt
done
