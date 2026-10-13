package main

import (
	"fmt"
	"strings"
)

const minAutoPortScore = 2

type portInfo struct {
	Name         string
	Product      string
	Manufacturer string
	VID          string
}

func findPort(preferred string) (string, error) {
	if preferred != "" {
		return preferred, nil
	}

	ports, err := listPorts()
	if err != nil {
		return "", err
	}
	name, score := pickScoredPort(ports)
	if score < minAutoPortScore {
		return "", fmt.Errorf("no serial port found. Plug in ESP-Mosaico or pass --port")
	}
	return name, nil
}

func pickScoredPort(ports []portInfo) (string, int) {
	if len(ports) == 0 {
		return "", 0
	}
	bestName := ports[0].Name
	bestScore := scorePort(ports[0])
	for _, port := range ports[1:] {
		if s := scorePort(port); s > bestScore {
			bestScore = s
			bestName = port.Name
		}
	}
	return bestName, bestScore
}

func scorePort(port portInfo) int {
	blob := strings.ToLower(strings.Join([]string{
		port.Name, port.Product, port.Manufacturer, port.VID,
	}, " "))
	for _, skip := range []string{"bluetooth-incoming", "incoming-port", "debug-console", "usbmodem1101"} {
		if strings.Contains(blob, skip) {
			return -100
		}
	}
	score := 0
	for _, token := range []string{"rainmonitor", "mosaico", "espressif", "usbmodem", "usbserial", "ttyacm"} {
		if strings.Contains(blob, token) {
			score++
		}
	}
	if strings.EqualFold(port.VID, "303A") {
		score += 2
	}
	// Prefer /dev/cu.* on macOS; tty.* blocks on carrier detect.
	if strings.Contains(port.Name, "/cu.") {
		score++
	}
	if strings.Contains(port.Name, "/tty.") {
		score--
	}
	return score
}
