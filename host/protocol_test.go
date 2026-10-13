package main

import (
	"strings"
	"testing"
)

func TestSanitizeModel(t *testing.T) {
	got := sanitizeModel("Apple M5, \"Max\" = GPU", 36)
	if strings.ContainsAny(got, `",=`) {
		t.Fatalf("forbidden chars remain: %q", got)
	}
	if got != "Apple M5 Max GPU" {
		t.Fatalf("got %q", got)
	}
	if got := sanitizeModel("0123456789abcdefghij0123456789abcdefEXTRA", 36); len(got) != 36 {
		t.Fatalf("limit: %q (%d)", got, len(got))
	}
}

func TestEncodeLine(t *testing.T) {
	line := string(encodeLine(sample{
		CPU:          12.3,
		GPU:          45.6,
		GPUValid:     true,
		FreqMHz:      4000,
		FreqValid:    true,
		GPUFreqMHz:   1296,
		GPUFreqValid: true,
		MemPct:       70.5,
		MemUsedGB:    16.25,
		MemTotalGB:   32,
		MemValid:     true,
		NetUpKBs:     1.5,
		NetDownKBs:   8.2,
		CPUModel:     "Apple M5",
		GPUModel:     "Apple M5",
		Cores:        []float64{12.3, 4.5, 80},
	}))
	for _, part := range []string{
		"cpu=12.3",
		"gpu=45.6",
		"cpu_freq=4000",
		"gpu_freq=1296",
		"mem=70.5",
		"mem_used=16.25",
		"mem_total=32.00",
		"net_up=1.5",
		"net_down=8.2",
		`cpu_model="Apple M5"`,
		`gpu_model="Apple M5"`,
		"cores=12.3;4.5;80.0",
	} {
		if !strings.Contains(line, part) {
			t.Fatalf("missing %s in %s", part, line)
		}
	}
	if !strings.HasSuffix(line, "\n") {
		t.Fatal("line must end with newline")
	}
}
