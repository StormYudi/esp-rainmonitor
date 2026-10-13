package main

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/kardianos/service"
)

type collectorProgram struct {
	cfg  collectorConfig
	stop chan struct{}
}

func (p *collectorProgram) Start(s service.Service) error {
	p.stop = make(chan struct{})
	go func() {
		_ = runCollector(p.cfg, p.stop)
	}()
	return nil
}

func (p *collectorProgram) Stop(s service.Service) error {
	if p.stop != nil {
		close(p.stop)
	}
	return nil
}

func serviceBinaryName() string {
	if runtime.GOOS == "windows" {
		return hostBinaryBase + ".exe"
	}
	return hostBinaryBase
}

func systemInstallDir() string {
	switch runtime.GOOS {
	case "windows":
		base := os.Getenv("ProgramFiles")
		if base == "" {
			base = `C:\Program Files`
		}
		return filepath.Join(base, installDirName)
	case "darwin":
		return filepath.Join("/Library/Application Support", installDirName)
	default:
		return "/usr/local/libexec/rainmonitor"
	}
}

func userInstallDir() string {
	home, err := os.UserHomeDir()
	if err != nil {
		home = "."
	}
	switch runtime.GOOS {
	case "windows":
		base := os.Getenv("LOCALAPPDATA")
		if base == "" {
			base = filepath.Join(home, "AppData", "Local")
		}
		return filepath.Join(base, installDirName)
	case "darwin":
		return filepath.Join(home, "Library", "Application Support", installDirName)
	default:
		return filepath.Join(home, ".local", "libexec", "rainmonitor")
	}
}

func installDir(userLevel bool) string {
	if userLevel {
		return userInstallDir()
	}
	return systemInstallDir()
}

func newHostService(cfg collectorConfig, executable string, userLevel bool) (service.Service, error) {
	options := service.KeyValue{
		"StartType":              "automatic",
		"OnFailure":              "restart",
		"OnFailureDelayDuration": "2s",
		"OnFailureResetPeriod":   10,
		"DelayedAutoStart":       false,
		"Restart":                "always",
		"KeepAlive":              true,
		"RunAtLoad":              true,
		"LogOutput":              true,
		"UserService":            userLevel,
	}
	svcCfg := &service.Config{
		Name:        serviceName,
		DisplayName: serviceDisplayName,
		Description: serviceDescription,
		Arguments:   serviceArguments(cfg),
		Option:      options,
	}
	if executable != "" {
		svcCfg.Executable = executable
		svcCfg.WorkingDirectory = filepath.Dir(executable)
	}
	return service.New(&collectorProgram{cfg: cfg}, svcCfg)
}

func serviceArguments(cfg collectorConfig) []string {
	args := []string{"--service", "--transport", cfg.Transport}
	if cfg.Port != "" {
		args = append(args, "--port", cfg.Port)
	}
	if cfg.Baud != 0 && cfg.Baud != 115200 {
		args = append(args, "--baud", strconv.Itoa(cfg.Baud))
	}
	if cfg.Interval != 0 && cfg.Interval != time.Second {
		args = append(args, "--interval", cfg.Interval.String())
	}
	if cfg.BLEName != "" && cfg.BLEName != defaultBLEName {
		args = append(args, "--ble-name", cfg.BLEName)
	}
	if cfg.BLEWait != 0 && cfg.BLEWait != 4*time.Second {
		args = append(args, "--ble-wait", cfg.BLEWait.String())
	}
	return args
}

func installArgv(cfg collectorConfig) []string {
	return append([]string{"install"}, serviceArguments(cfg)[1:]...)
}

func installService(cfg collectorConfig) error {
	if processElevated() {
		return installServiceAs(cfg, false)
	}
	if runtime.GOOS == "windows" {
		fmt.Println("安装系统服务需要管理员权限，即将弹出 UAC...")
		return elevateAndWait(installArgv(cfg), true)
	}
	if isTerminal() {
		fmt.Println("安装开机自启的系统服务需要 root，将调用 sudo...")
		if err := elevateAndWait(installArgv(cfg), true); err == nil {
			return nil
		} else {
			fmt.Fprintf(os.Stderr, "sudo 失败: %v\n", err)
		}
	}
	fmt.Println("改为安装当前用户服务（登录后自动启动）。")
	return installServiceAs(cfg, true)
}

func installServiceAs(cfg collectorConfig, userLevel bool) (err error) {
	defer func() {
		if err != nil {
			writeElevatedResult(cfg.ResultPath, "", err)
		}
	}()
	dest, err := installBinary(userLevel)
	if err != nil {
		return err
	}
	cfg.Service = true
	cfg.Quiet = true
	s, err := newHostService(cfg, dest, userLevel)
	if err != nil {
		return err
	}
	if st, err := s.Status(); err == nil && (st == service.StatusRunning || st == service.StatusStopped) {
		_ = s.Stop()
		if err := s.Uninstall(); err != nil {
			return fmt.Errorf("无法覆盖已有服务: %w", err)
		}
	}
	if err := s.Install(); err != nil {
		_ = s.Stop()
		_ = s.Uninstall()
		if retry := s.Install(); retry != nil {
			return fmt.Errorf("安装服务失败: %w", err)
		}
	}
	if userLevel && runtime.GOOS == "linux" {
		enableLinger()
	}
	if err := s.Start(); err != nil {
		return fmt.Errorf("服务已安装，但启动失败: %w", err)
	}
	kind := "系统服务（开机自动启动）"
	if userLevel {
		kind = "用户服务（登录后自动启动）"
	}
	msg := fmt.Sprintf("已安装并启动 %s。\n平台: %s\n程序: %s", kind, service.Platform(), dest)
	fmt.Println(msg)
	writeElevatedResult(cfg.ResultPath, msg, nil)
	return nil
}

func uninstallService(cfg collectorConfig) error {
	info := detectService()
	if !info.Installed {
		err := fmt.Errorf("未找到已安装的服务")
		writeElevatedResult(cfg.ResultPath, "", err)
		return err
	}
	if !info.UserLevel && !processElevated() {
		if runtime.GOOS == "windows" {
			fmt.Println("卸载系统服务需要管理员权限，即将弹出 UAC...")
			return elevateAndWait([]string{"uninstall"}, false)
		}
		if isTerminal() {
			fmt.Println("卸载系统服务需要 root，将调用 sudo...")
			if err := elevateAndWait([]string{"uninstall"}, false); err == nil {
				return nil
			}
		}
	}
	return uninstallServiceAs(cfg, info.UserLevel)
}

func uninstallServiceAs(cfg collectorConfig, userLevel bool) (err error) {
	defer func() {
		if err != nil {
			writeElevatedResult(cfg.ResultPath, "", err)
		}
	}()
	s, err := newHostService(collectorConfig{}, "", userLevel)
	if err != nil {
		return err
	}
	_ = s.Stop()
	if err := s.Uninstall(); err != nil {
		return fmt.Errorf("卸载服务失败: %w", err)
	}
	if rmErr := removeInstalledBinary(userLevel); rmErr != nil {
		fmt.Fprintf(os.Stderr, "服务已卸载，但未能删除程序文件: %v\n", rmErr)
	}
	fmt.Println("已卸载服务。")
	writeElevatedResult(cfg.ResultPath, "已卸载服务。", nil)
	return nil
}

func controlService(action string) error {
	s, _, err := lookupInstalledService()
	if err != nil {
		return err
	}
	return service.Control(s, action)
}

func lookupInstalledService() (service.Service, bool, error) {
	info := detectService()
	if !info.Installed {
		return nil, false, fmt.Errorf("未找到已安装的服务: %w", service.ErrNotInstalled)
	}
	s, err := newHostService(collectorConfig{}, "", info.UserLevel)
	if err != nil {
		return nil, false, err
	}
	return s, info.UserLevel, nil
}

func runAsService(cfg collectorConfig) error {
	cfg.Service = true
	cfg.Quiet = true
	s, err := newHostService(cfg, "", false)
	if err != nil {
		return err
	}
	return s.Run()
}

func installBinary(userLevel bool) (string, error) {
	src, err := os.Executable()
	if err != nil {
		return "", err
	}
	src, err = filepath.EvalSymlinks(src)
	if err != nil {
		return "", err
	}
	dir := installDir(userLevel)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return "", fmt.Errorf("创建安装目录失败: %w", err)
	}
	dest := filepath.Join(dir, serviceBinaryName())
	if sameFile(src, dest) {
		return dest, nil
	}
	if err := copyFile(src, dest); err != nil {
		return "", fmt.Errorf("复制程序到 %s 失败: %w", dest, err)
	}
	if runtime.GOOS != "windows" {
		_ = os.Chmod(dest, 0755)
	}
	return dest, nil
}

func removeInstalledBinary(userLevel bool) error {
	dir := installDir(userLevel)
	dest := filepath.Join(dir, serviceBinaryName())
	self, err := os.Executable()
	if err == nil {
		if resolved, err := filepath.EvalSymlinks(self); err == nil {
			self = resolved
		}
		if sameFile(self, dest) {
			return fmt.Errorf("当前正在运行该程序，请稍后手动删除 %s", dest)
		}
	}
	if err := os.Remove(dest); err != nil && !os.IsNotExist(err) {
		return err
	}
	_ = os.Remove(filepath.Join(dir, hostBinaryBase+".log"))
	if entries, err := os.ReadDir(dir); err == nil && len(entries) == 0 {
		_ = os.Remove(dir)
	}
	return nil
}

func sameFile(a, b string) bool {
	ai, err1 := os.Stat(a)
	bi, err2 := os.Stat(b)
	if err1 != nil || err2 != nil {
		return false
	}
	return os.SameFile(ai, bi)
}

func copyFile(src, dest string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	tmp := dest + ".tmp"
	out, err := os.OpenFile(tmp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0755)
	if err != nil {
		return err
	}
	_, copyErr := io.Copy(out, in)
	closeErr := out.Close()
	if copyErr != nil {
		_ = os.Remove(tmp)
		return copyErr
	}
	if closeErr != nil {
		_ = os.Remove(tmp)
		return closeErr
	}
	if err := os.Rename(tmp, dest); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

func quoteWindowsArg(s string) string {
	if s == "" {
		return `""`
	}
	if !strings.ContainsAny(s, " \t\"") {
		return s
	}
	return `"` + strings.ReplaceAll(s, `"`, `\"`) + `"`
}

func enableLinger() {
	u, err := user.Current()
	if err != nil || u.Username == "" {
		return
	}
	_ = exec.Command("loginctl", "enable-linger", u.Username).Run()
}
