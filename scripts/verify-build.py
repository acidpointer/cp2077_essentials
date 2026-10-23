#!/usr/bin/env python3
import argparse
import common as c
import distribution
import verification


def main():
    parser = argparse.ArgumentParser(description='Verify built release archives and optional installed-game hashes.')
    parser.add_argument('--config', choices=['Release', 'Debug'], default='Release')
    parser.add_argument('--dist-dir')
    parser.add_argument('--addresses')
    args = parser.parse_args()
    verification.distribution(distribution.output(args.config, args.dist_dir))
    if args.addresses:
        verification.addresses(args.addresses)


if __name__ == '__main__':
    c.entry(main)
