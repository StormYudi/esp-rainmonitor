//go:build !windows

package main

import "github.com/kardianos/service"

func detectService() serviceInfo {
	for _, userLevel := range []bool{false, true} {
		s, err := newHostService(collectorConfig{}, "", userLevel)
		if err != nil {
			continue
		}
		st, err := s.Status()
		if err != nil {
			continue
		}
		return serviceInfo{
			Installed: true,
			UserLevel: userLevel,
			Running:   st == service.StatusRunning,
			Stopped:   st == service.StatusStopped,
		}
	}
	return serviceInfo{}
}
