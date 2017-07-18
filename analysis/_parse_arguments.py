import re

def parse_args(s):
  args = {}
  for i in s:
    m = re.search("-([^= ]+)=([^ ]+)", i)
    if m:
      args[m.group(1).lower()] = m.group(2)
  return args
