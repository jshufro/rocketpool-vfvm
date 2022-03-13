package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
)

func main() {
	var pretty bytes.Buffer

	buf16, err := getArtifacts(Half)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1);
	}

	buf32, err := getArtifacts(Full)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1);
	}

	output := fmt.Sprintf("{\"16\":%s,\"32\":%s}", string(buf16), string(buf32))
	if err := json.Indent(&pretty, []byte(output), "", "  "); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1);
	}

	fmt.Println(pretty.String())
}
