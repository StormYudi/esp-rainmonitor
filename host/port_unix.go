//go:build !windows

package main

import (
	"os"
	"path/filepath"
	"strings"

	"go.bug.st/serial"
)

func listPorts() ([]portInfo, error) {
	names, err := serial.GetPortsList()
	if err != nil {
		return nil, err
	}
	out := make([]portInfo, 0, len(names))
	for _, name := range names {
		info := portInfo{Name: name}
		info.Product, info.Manufacturer, info.VID = usbMeta(name)
		out = append(out, info)
	}
	return out, nil
}

func usbMeta(devName string) (product, manufacturer, vid string) {
	base := filepath.Base(devName)
	dir := filepath.Join("/sys/class/tty", base, "device")
	for i := 0; i < 8; i++ {
		if product == "" {
			product = readTrim(filepath.Join(dir, "product"))
		}
		if manufacturer == "" {
			manufacturer = readTrim(filepath.Join(dir, "manufacturer"))
		}
		if vid == "" {
			vid = strings.ToUpper(readTrim(filepath.Join(dir, "idVendor")))
		}
		if product != "" && vid != "" {
			return product, manufacturer, vid
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return product, manufacturer, vid
}

func readTrim(path string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(b))
}
