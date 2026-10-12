"""Export one explicit or witness-prefix query. Never modify the input JSONL."""
import argparse
import json
from pathlib import Path


def specification(row):
    x, y = row['x'], row['y']
    required = row['i']
    nr = row['r_size']
    if x == y or len(set(required)) != len(required):
        raise ValueError('invalid endpoints or duplicate required vertex')
    if row['i_size'] != len(required) or nr < len(required):
        raise ValueError('inconsistent query sizes')
    first = f'{x} {y} {len(required)} {nr}\n'
    first += ' '.join(map(str, required)) + '\n'
    if 'r' in row:
        allowed = row['r']
        if len(allowed) != nr or len(set(allowed)) != nr:
            raise ValueError('inconsistent or duplicate allowed vertices')
        if not set(required) <= set(allowed) or {x, y} & set(allowed):
            raise ValueError('invalid I/R containment or endpoints in R')
        return first + ' '.join(map(str, allowed)) + '\n', 'explicit'
    rep = row['r_representation']
    if rep['type'] != 'witness-prefix-feistel-skip-v1':
        raise ValueError('unsupported compact representation')
    anchor = rep['anchor']
    if (rep['prefix_size'] != nr or nr > row['universe_size'] or
            len(set(anchor)) != len(anchor) or len(anchor) > nr or
            not set(required) <= set(anchor) or {x, y} & set(anchor)):
        raise ValueError('invalid compact anchor or prefix')
    return (first + f"{rep['seed']} {len(anchor)}\n" +
            ' '.join(map(str, anchor)) + '\n'), 'compact'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('queries', type=Path)
    parser.add_argument('query_id', type=int)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    found = None
    with args.queries.open() as source:
        for line in source:
            row = json.loads(line)
            if row['query_id'] == args.query_id:
                if found is not None:
                    raise ValueError('duplicate query ID in input')
                found = row
    if found is None:
        raise ValueError('query ID not found')
    text, encoding = specification(found)
    with args.output.open('x') as output:
        output.write(text)
    print(f'encoding={encoding}; append compact to fdsolveir only for compact input')


if __name__ == '__main__':
    main()
