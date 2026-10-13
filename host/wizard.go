package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"runtime"
	"strconv"
	"strings"

	"github.com/kardianos/service"
)

func isTerminal() bool {
	fi, err := os.Stdin.Stat()
	if err != nil {
		return false
	}
	return fi.Mode()&os.ModeCharDevice != 0
}

func runWizard(cfg collectorConfig) error {
	fmt.Println(productName + " 采集端")
	fmt.Println()
	fmt.Printf("系统: %s/%s\n", runtime.GOOS, runtime.GOARCH)
	fmt.Printf("服务后端: %s\n", service.Platform())
	fmt.Printf("当前状态: %s\n", serviceStatusText())
	fmt.Println()
	fmt.Println("请选择：")
	fmt.Println("  1) 安装后台服务（自动启动，后台运行）")
	fmt.Println("  2) 卸载后台服务")
	fmt.Println("  3) 在当前窗口前台运行（不安装）")
	fmt.Println("  4) 退出")
	fmt.Println()

	choice, err := readMenuChoice(os.Stdin, os.Stdout)
	if err != nil {
		return err
	}
	switch choice {
	case 1:
		err = installService(cfg)
	case 2:
		err = uninstallService(cfg)
	case 3:
		return runCollector(cfg, nil)
	case 4:
		return nil
	}
	fmt.Println()
	if err != nil {
		fmt.Println("失败:", err)
	}
	fmt.Printf("当前状态: %s\n", serviceStatusText())
	pauseIfInteractive()
	return err
}

func readMenuChoice(in io.Reader, out io.Writer) (int, error) {
	scanner := bufio.NewScanner(in)
	for {
		fmt.Fprint(out, "输入序号 [1-4]: ")
		if !scanner.Scan() {
			if err := scanner.Err(); err != nil {
				return 0, err
			}
			return 0, fmt.Errorf("已取消")
		}
		n, ok := parseMenuChoice(scanner.Text())
		if ok {
			return n, nil
		}
		fmt.Fprintln(out, "请输入 1、2、3 或 4。")
	}
}

func parseMenuChoice(s string) (int, bool) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, false
	}
	n, err := strconv.Atoi(s)
	if err != nil || n < 1 || n > 4 {
		return 0, false
	}
	return n, true
}

func pauseIfInteractive() {
	if !isTerminal() {
		return
	}
	fmt.Print("按 Enter 退出...")
	_, _ = bufio.NewReader(os.Stdin).ReadBytes('\n')
}
