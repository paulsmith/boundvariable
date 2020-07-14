#!/bin/sh
set -e -x
cc -Wall -pedantic -std=c11 -O3 -g -o um-32 um-32.c
