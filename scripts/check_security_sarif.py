import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
files = sorted(root.glob("*.sarif")) if root.is_dir() else [root]
if not files:
    raise SystemExit("No SARIF files found")
blocked = 0
for path in files:
    for run in json.loads(path.read_text(encoding="utf-8"))["runs"]:
        rules = {rule["id"]: rule for rule in run.get("tool", {}).get("driver", {}).get("rules", [])}
        for result in run.get("results", []):
            rule_id = result.get("ruleId", "")
            rule = rules.get(rule_id, {})
            severity = float(rule.get("properties", {}).get("security-severity", 0))
            if severity >= 7 or rule_id in {"cpp/overflowing-snprintf", "cpp/integer-multiplication-cast-to-long"}:
                blocked += 1
                print(json.dumps({"rule": rule_id, "locations": result.get("locations"), "fingerprints": result.get("partialFingerprints"), "message": result.get("message")}, ensure_ascii=False))
print(f"Blocking findings: {blocked}")
raise SystemExit(1 if blocked else 0)
