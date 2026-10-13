//go:build linux

package main

import (
	"bufio"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var (
	lspciGPURe = regexp.MustCompile(`(?:VGA|3D|Display)(?: compatible)? controller:\s*(.+)`)
	amdClkRe   = regexp.MustCompile(`(?i)(\d+(?:\.\d+)?)\s*Mhz\s*\*`)
)

func platformGPUUtil() (float64, bool) {
	return 0, false
}

func platformGPUFreq() (float64, bool) {
	globs := []string{
		"/sys/class/drm/card*/gt_act_freq_mhz",
		"/sys/class/drm/card*/gt_cur_freq_mhz",
		"/sys/class/drm/card*/gt/gt0/rps_act_freq_mhz",
		"/sys/class/drm/card*/device/gt_act_freq_mhz",
	}
	for _, pattern := range globs {
		matches, _ := filepath.Glob(pattern)
		for _, path := range matches {
			if v, ok := readFloatFile(path); ok && v > 0 {
				return v, true
			}
		}
	}
	matches, _ := filepath.Glob("/sys/class/drm/card*/device/pp_dpm_sclk")
	for _, path := range matches {
		b, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		if m := amdClkRe.FindStringSubmatch(string(b)); m != nil {
			if v, err := strconv.ParseFloat(m[1], 64); err == nil && v > 0 {
				return v, true
			}
		}
	}
	return 0, false
}

func readFloatFile(path string) (float64, bool) {
	b, err := os.ReadFile(path)
	if err != nil {
		return 0, false
	}
	v, err := strconv.ParseFloat(strings.TrimSpace(string(b)), 64)
	return v, err == nil
}

func platformCPUModel() string {
	f, err := os.Open("/proc/cpuinfo")
	if err != nil {
		return ""
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if strings.HasPrefix(strings.ToLower(line), "model name") {
			parts := strings.SplitN(line, ":", 2)
			if len(parts) == 2 {
				return strings.TrimSpace(parts[1])
			}
		}
	}
	return ""
}

func platformGPUModel() string {
	out, ok := runCmd(4*time.Second, "lspci")
	if !ok {
		return ""
	}
	m := lspciGPURe.FindStringSubmatch(out)
	if m == nil {
		return ""
	}
	return strings.TrimSpace(m[1])
}

func platformCPUFreqMHz() float64 {
	f, err := os.Open("/proc/cpuinfo")
	if err != nil {
		return 0
	}
	defer f.Close()
	var found float64
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if !strings.Contains(strings.ToLower(line), "cpu mhz") {
			continue
		}
		parts := strings.SplitN(line, ":", 2)
		if len(parts) != 2 {
			continue
		}
		if v, err := strconv.ParseFloat(strings.TrimSpace(parts[1]), 64); err == nil {
			found = v
		}
	}
	return found
}
