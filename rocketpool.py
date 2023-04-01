#!/usr/bin/env python3
import subprocess
import json

def getArtifacts(d):

    cmdFmt = "docker exec rocketpool_api /go/bin/rocketpool api minipool get-vanity-artifacts {} 0"
    if d == "16":
        cmd = cmdFmt.format("16000000000000000000")
    else:
        cmd = cmdFmt.format("32000000000000000000")

    if not cmd:
        raise Exception("Invalid deposit amount. \"16\" and \"32\" are the only supported amounts")

    output = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)
    if not output:
        raise Exception("Failed to run command")

    parsed = json.loads(output)
    if not parsed:
        raise Exception(f"Error parsing JSON: {output}")

    if "error" in parsed and parsed["error"] != "":
        raise Exception(f"Error from rocketpool api: {parsed['error']}")

    if parsed["status"] != "success":
        raise Exception(f"Unknown error occured handling response {output}")

    return parsed

print(json.dumps({"atlas": getArtifacts("16")}, indent=4, sort_keys=True))
