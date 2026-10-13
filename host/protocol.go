package main

import (
	"fmt"
	"strings"
	"time"
	"unicode"
)

func sanitizeModel(name string, limit int) string {
	var b strings.Builder
	b.Grow(len(name))
	for _, r := range name {
		if r >= 32 && r < 127 && r != '"' && r != ',' && r != '=' {
			b.WriteRune(r)
		} else if unicode.IsSpace(r) {
			b.WriteByte(' ')
		} else {
			b.WriteByte(' ')
		}
	}
	cleaned := strings.Join(strings.Fields(b.String()), " ")
	if len(cleaned) > limit {
		return cleaned[:limit]
	}
	return cleaned
}

func encodeLine(s sample) []byte {
	parts := []string{fmt.Sprintf("cpu=%.1f", s.CPU)}
	if s.GPUValid {
		parts = append(parts, fmt.Sprintf("gpu=%.1f", s.GPU))
	}
	if s.FreqValid {
		parts = append(parts, fmt.Sprintf("cpu_freq=%.0f", s.FreqMHz))
	}
	if s.GPUFreqValid {
		parts = append(parts, fmt.Sprintf("gpu_freq=%.0f", s.GPUFreqMHz))
	}
	if s.MemValid {
		parts = append(parts, fmt.Sprintf("mem=%.1f", s.MemPct))
		parts = append(parts, fmt.Sprintf("mem_used=%.2f", s.MemUsedGB))
		parts = append(parts, fmt.Sprintf("mem_total=%.2f", s.MemTotalGB))
	}
	parts = append(parts, fmt.Sprintf("net_up=%.1f", s.NetUpKBs))
	parts = append(parts, fmt.Sprintf("net_down=%.1f", s.NetDownKBs))
	parts = append(parts, fmt.Sprintf("time=\"%s\"", time.Now().Format("15:04:05")))
	if s.CPUModel != "" {
		parts = append(parts, fmt.Sprintf("cpu_model=\"%s\"", s.CPUModel))
	}
	if s.GPUModel != "" {
		parts = append(parts, fmt.Sprintf("gpu_model=\"%s\"", s.GPUModel))
	}
	if len(s.Cores) > 0 {
		var b strings.Builder
		b.WriteString("cores=")
		for i, c := range s.Cores {
			if i > 0 {
				b.WriteByte(';')
			}
			fmt.Fprintf(&b, "%.1f", c)
		}
		parts = append(parts, b.String())
	}
	return []byte(strings.Join(parts, ",") + "\n")
}
