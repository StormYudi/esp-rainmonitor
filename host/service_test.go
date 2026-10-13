package main

import (
	"bytes"
	"fmt"
	"os"
	"runtime"
	"strings"
	"testing"
	"time"
)

func TestParseMenuChoice(t *testing.T) {
	cases := []struct {
		in     string
		want   int
		wantOK bool
	}{
		{"1", 1, true},
		{" 2 ", 2, true},
		{"4", 4, true},
		{"0", 0, false},
		{"5", 0, false},
		{"", 0, false},
		{"install", 0, false},
	}
	for _, c := range cases {
		got, ok := parseMenuChoice(c.in)
		if ok != c.wantOK || got != c.want {
			t.Fatalf("parseMenuChoice(%q) = %d, %v; want %d, %v", c.in, got, ok, c.want, c.wantOK)
		}
	}
}

func TestParseArgsSubcommandAndFlags(t *testing.T) {
	cfg, cmd, err := parseArgs([]string{"install", "--transport", "usb", "--port", "COM5"}, ioDiscard())
	if err != nil {
		t.Fatal(err)
	}
	if cmd != cmdInstall {
		t.Fatalf("cmd %q", cmd)
	}
	if cfg.Transport != "usb" || cfg.Port != "COM5" {
		t.Fatalf("cfg %+v", cfg)
	}
}

func TestParseArgsRejectsUnknownCommand(t *testing.T) {
	_, _, err := parseArgs([]string{"serve"}, ioDiscard())
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestServiceArgumentsOmitsDefaults(t *testing.T) {
	args := serviceArguments(collectorConfig{
		Transport: "auto",
		Baud:      115200,
		Interval:  time.Second,
		BLEName:   defaultBLEName,
		BLEWait:   4 * time.Second,
	})
	joined := strings.Join(args, " ")
	if !strings.HasPrefix(joined, "--service --transport auto") {
		t.Fatalf("args %v", args)
	}
	if strings.Contains(joined, "--port") || strings.Contains(joined, "--baud") {
		t.Fatalf("defaults should be omitted: %v", args)
	}

	args = serviceArguments(collectorConfig{Transport: "ble", Port: "COM7", Baud: 9600})
	if !containsAll(args, "--service", "--transport", "ble", "--port", "COM7", "--baud", "9600") {
		t.Fatalf("args %v", args)
	}
}

func TestInstallArgvDropsServiceFlag(t *testing.T) {
	args := installArgv(collectorConfig{Transport: "auto"})
	if args[0] != "install" {
		t.Fatalf("args %v", args)
	}
	for _, a := range args {
		if a == "--service" {
			t.Fatal("install argv should not include --service")
		}
	}
}

func TestShouldShowBanner(t *testing.T) {
	if !shouldShowBanner(collectorConfig{}, "") || !shouldShowBanner(collectorConfig{}, cmdRun) {
		t.Fatal("wizard and run should show banner")
	}
	if shouldShowBanner(collectorConfig{Service: true}, cmdRun) {
		t.Fatal("service mode should stay quiet")
	}
	if shouldShowBanner(collectorConfig{}, cmdInstall) {
		t.Fatal("install should not print the banner")
	}
}

func TestBannerArt(t *testing.T) {
	if !strings.Contains(rainMonitorBanner, "RainMonitor") && !strings.Contains(rainMonitorBanner, "| |__) |") {
		t.Fatal("banner should contain the RainMonitor wordmark")
	}
}

func TestParseArgsResultPath(t *testing.T) {
	cfg, cmd, err := parseArgs([]string{"uninstall", "--result", "/tmp/out.json"}, ioDiscard())
	if err != nil {
		t.Fatal(err)
	}
	if cmd != cmdUninstall || cfg.ResultPath != "/tmp/out.json" {
		t.Fatalf("cmd=%q cfg=%+v", cmd, cfg)
	}
}

func TestElevatedResultRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := dir + "/result.json"
	writeElevatedResult(path, "已卸载服务。", nil)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	out, err := decodeElevatedResult(data)
	if err != nil || !out.OK || out.Msg != "已卸载服务。" {
		t.Fatalf("got %+v err %v", out, err)
	}

	writeElevatedResult(path, "", fmt.Errorf("未找到已安装的服务"))
	data, err = os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	out, err = decodeElevatedResult(data)
	if err != nil || out.OK || out.Err != "未找到已安装的服务" {
		t.Fatalf("got %+v err %v", out, err)
	}
}

func TestServiceStatusTextWhenMissing(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Windows status comes from SCM")
	}
	if got := serviceStatusText(); got != "未安装" && !strings.Contains(got, "服务") {
		t.Fatalf("unexpected status %q", got)
	}
}

func TestQuoteWindowsArg(t *testing.T) {
	if quoteWindowsArg("COM5") != "COM5" {
		t.Fatal("plain arg")
	}
	if quoteWindowsArg("Rain Monitor") != `"Rain Monitor"` {
		t.Fatalf("quoted: %q", quoteWindowsArg("Rain Monitor"))
	}
}

func ioDiscard() *bytes.Buffer {
	return &bytes.Buffer{}
}

func containsAll(args []string, want ...string) bool {
	joined := " " + strings.Join(args, " ") + " "
	for _, w := range want {
		if !strings.Contains(joined, " "+w+" ") {
			return false
		}
	}
	return true
}
