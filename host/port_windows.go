//go:build windows

package main

import "go.bug.st/serial/enumerator"

func listPorts() ([]portInfo, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return nil, err
	}
	out := make([]portInfo, 0, len(ports))
	for _, port := range ports {
		out = append(out, portInfo{
			Name:         port.Name,
			Product:      port.Product,
			Manufacturer: port.Manufacturer,
			VID:          port.VID,
		})
	}
	return out, nil
}
