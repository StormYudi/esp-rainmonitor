//go:build nobile

package main

import (
	"fmt"
	"time"
)

func openBLE(wantName string, timeout time.Duration) (lineSink, error) {
	return nil, fmt.Errorf("bluetooth support not in this build")
}
