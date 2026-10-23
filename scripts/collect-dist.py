#!/usr/bin/env python3
import argparse
import common as c
import distribution


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('config', choices=['Release', 'Debug'])
    parser.add_argument('project', nargs='?', default='all')
    parser.add_argument('--dist-dir')
    args = parser.parse_args()
    selected = [name for name in c.select(args.project) if name != 'red4ext-sdk']
    distribution.collect(args.config, selected, args.dist_dir)


if __name__ == '__main__':
    c.entry(main)
