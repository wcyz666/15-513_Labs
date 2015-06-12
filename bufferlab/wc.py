#!/bin/env python
import re 
import sys

result = []
for line in sys.stdin:
    regex = re.match(r"^\s[^:]*:\s+(.*)\s+\w.*$", line)
    if (regex):
        result = result + regex.group(1).strip().split()
print (' '.join(result))
