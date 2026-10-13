package main

import (
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	"go.bug.st/serial"
)

type lineSink interface {
	Write(p []byte) (int, error)
	Close() error
}

var errPreferUSB = errors.New("usb available")

var usbBlocked string

func blockUSB(name string) {
	usbBlocked = name
}

func clearUSBBlock() {
	usbBlocked = ""
}

func openLink(mode, portName, bleName string, baud int, scanWait time.Duration) (lineSink, string, error) {
	mode = strings.ToLower(strings.TrimSpace(mode))
	if mode == "" {
		mode = "auto"
	}
	tryBLE := mode == "auto" || mode == "ble"
	tryUSB := mode == "auto" || mode == "usb"
	if !tryBLE && !tryUSB {
		return nil, "", fmt.Errorf("transport must be auto, ble, or usb")
	}

	var usbErr error
	if tryUSB {
		name, err := findPort(portName)
		if err == nil {
			port, err := serial.Open(name, &serial.Mode{BaudRate: baud})
			if err == nil {
				clearUSBBlock()
				time.Sleep(300 * time.Millisecond)
				return port, "usb " + name, nil
			}
			blockUSB(name)
			usbErr = fmt.Errorf("USB %s: %w", name, err)
		} else {
			usbErr = err
		}
		if mode == "usb" {
			return nil, "", usbErr
		}
		fmt.Fprintf(os.Stderr, "usb: %v; trying bluetooth\n", usbErr)
	}

	if !tryBLE {
		return nil, "", usbErr
	}
	sink, err := openBLE(bleName, scanWait)
	if err == nil {
		return sink, "bluetooth", nil
	}
	if usbErr != nil {
		return nil, "", fmt.Errorf("%v; %v", usbErr, err)
	}
	return nil, "", err
}

func usbPresent(preferred string) (string, bool) {
	ports, err := listPorts()
	if err != nil {
		return "", false
	}
	if preferred != "" {
		for _, port := range ports {
			if port.Name == preferred {
				return preferred, true
			}
		}
		return "", false
	}
	name, score := pickScoredPort(ports)
	if score < minAutoPortScore {
		return "", false
	}
	return name, true
}

func usbReady(preferred string) (string, bool) {
	name, ok := usbPresent(preferred)
	if !ok {
		clearUSBBlock()
		return "", false
	}
	if usbBlocked != "" && name == usbBlocked {
		return "", false
	}
	return name, true
}
