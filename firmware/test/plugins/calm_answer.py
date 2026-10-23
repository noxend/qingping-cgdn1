"""The alerts API's answer with no alert in it. Usage: calm_answer.py <answer> <out>"""

import json
import sys

answer = json.load(open(sys.argv[1], encoding="utf-8"))
answer["raions"], answer["oblasts"] = [], []
json.dump(answer, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False)
