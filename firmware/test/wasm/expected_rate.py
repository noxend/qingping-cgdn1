"""The National Bank's USD rate in <nbu>/usd.json rounded to kopecks (half up), written
to <nbu>/expected.txt. Usage: expected_rate.py <nbu>"""

import decimal
import json
import sys

rate = decimal.Decimal(str(json.load(open(sys.argv[1] + "/usd.json"))[0]["rate"]))
cents = rate.quantize(decimal.Decimal("0.01"), rounding=decimal.ROUND_HALF_UP)
open(sys.argv[1] + "/expected.txt", "w").write(f"{cents}\n")
