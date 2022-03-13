package main

import (
	"encoding/json"
	"fmt"
	"os/exec"
)

var cmdFmt string = "docker exec rocketpool_api /go/bin/rocketpool api minipool get-vanity-artifacts %s 0"

type Deposit uint8
const (
	Half Deposit = iota
	Full
)

type Artifacts struct {
	Status string `json:"status"`
	Error string `json:"error"`
	NodeAddress string `json:"nodeAddress"`
	MinipoolManagerAddress string `json:"minipoolManagerAddress"`
	InitHash string `json:"initHash"`
}

func getArtifacts(d Deposit) ([]byte, error) {
	var cmd string
	var out Artifacts

	if d == Half {
		cmd = fmt.Sprintf(cmdFmt, "16000000000000000000")
	} else {
		cmd = fmt.Sprintf(cmdFmt, "32000000000000000000")
	}

	c := exec.Command("sh", "-c", cmd)
	ret, err := c.Output()
	if err != nil {
		return []byte{}, err
	}

	if err = json.Unmarshal(ret, &out); err != nil {
		return []byte{}, err
	}

	if out.Error != "" {
		return []byte{}, fmt.Errorf("Error querying rocketpool node: %s\n", out.Error)
	}

	return ret, nil
}
