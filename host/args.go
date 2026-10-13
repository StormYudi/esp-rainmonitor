package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	"github.com/kardianos/service"
)

const (
	cmdInstall   = "install"
	cmdUninstall = "uninstall"
	cmdRun       = "run"
	cmdStart     = "start"
	cmdStop      = "stop"
	cmdRestart   = "restart"
)

func parseArgs(args []string, output io.Writer) (collectorConfig, string, error) {
	fs := flag.NewFlagSet(hostBinaryBase, flag.ContinueOnError)
	fs.SetOutput(output)
	fs.Usage = func() {
		fmt.Fprintf(fs.Output(), `%s [install|uninstall|run|start|stop|restart] [flags]

直接运行（无参数）进入安装/卸载向导。
install    安装开机自启服务并在后台启动采集
uninstall  卸载服务
run        在当前窗口前台运行采集（不安装）
start|stop|restart  控制已安装的服务

`, hostBinaryBase)
		fs.PrintDefaults()
	}

	cfg := collectorConfig{
		Baud:      115200,
		Interval:  time.Second,
		Transport: "auto",
		BLEName:   defaultBLEName,
		BLEWait:   4 * time.Second,
	}
	fs.StringVar(&cfg.Port, "port", "", "Serial port, such as COM5 or /dev/cu.usbmodem3101")
	fs.IntVar(&cfg.Baud, "baud", 115200, "Serial baud rate")
	fs.DurationVar(&cfg.Interval, "interval", time.Second, "Sample period in seconds (Task Manager High = 1s)")
	fs.BoolVar(&cfg.Once, "once", false, "Send one sample and exit")
	fs.StringVar(&cfg.Transport, "transport", "auto", "auto (USB if plugged, else BLE), ble, or usb")
	fs.StringVar(&cfg.BLEName, "ble-name", defaultBLEName, "BLE advertised name")
	fs.DurationVar(&cfg.BLEWait, "ble-wait", 4*time.Second, "How long to scan for the device over Bluetooth")
	fs.BoolVar(&cfg.Service, "service", false, "Run as the installed OS service (used by the service manager)")
	fs.StringVar(&cfg.ResultPath, "result", "", "Internal: write elevated install/uninstall result here")

	cmd := ""
	rest := args
	if len(rest) > 0 && !strings.HasPrefix(rest[0], "-") {
		cmd = rest[0]
		rest = rest[1:]
	}
	if cmd != "" && !validCommand(cmd) {
		fs.Usage()
		return collectorConfig{}, "", fmt.Errorf("unknown command %q", cmd)
	}
	if err := fs.Parse(rest); err != nil {
		return collectorConfig{}, "", err
	}
	return cfg, cmd, nil
}

func validCommand(cmd string) bool {
	switch cmd {
	case cmdInstall, cmdUninstall, cmdRun, cmdStart, cmdStop, cmdRestart:
		return true
	default:
		return false
	}
}

func dispatch(cfg collectorConfig, cmd string) error {
	switch cmd {
	case cmdInstall:
		return installService(cfg)
	case cmdUninstall:
		return uninstallService(cfg)
	case cmdRun:
		return runCollector(cfg, nil)
	case cmdStart, cmdStop, cmdRestart:
		return controlService(cmd)
	}

	if cfg.Service || !service.Interactive() {
		return runAsService(cfg)
	}
	if isTerminal() {
		return runWizard(cfg)
	}
	fmt.Fprintf(os.Stderr, "打开终端运行此程序以安装或卸载服务，或使用: %s run\n", hostBinaryBase)
	return fmt.Errorf("no command")
}
