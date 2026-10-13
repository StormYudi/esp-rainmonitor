//go:build darwin

package main

import "os"

func detectService() serviceInfo {
	for _, userLevel := range []bool{false, true} {
		if _, err := os.Stat(launchdPlistPath(userLevel)); err != nil {
			continue
		}
		running := installedBinaryRunning(userLevel)
		return serviceInfo{
			Installed: true,
			UserLevel: userLevel,
			Running:   running,
			Stopped:   !running,
		}
	}
	return serviceInfo{}
}
