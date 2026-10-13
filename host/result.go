package main

import (
	"encoding/json"
	"fmt"
	"os"
	"time"
)

type elevatedResult struct {
	OK  bool   `json:"ok"`
	Msg string `json:"msg,omitempty"`
	Err string `json:"err,omitempty"`
}

func writeElevatedResult(path, msg string, err error) {
	if path == "" {
		return
	}
	out := elevatedResult{OK: err == nil, Msg: msg}
	if err != nil {
		out.Err = err.Error()
	}
	data, marshalErr := json.Marshal(out)
	if marshalErr != nil {
		return
	}
	tmp := path + ".tmp"
	if writeErr := os.WriteFile(tmp, data, 0600); writeErr != nil {
		return
	}
	_ = os.Rename(tmp, path)
}

func decodeElevatedResult(data []byte) (elevatedResult, error) {
	var out elevatedResult
	if err := json.Unmarshal(data, &out); err != nil {
		return elevatedResult{}, err
	}
	return out, nil
}

func waitElevatedResult(path string, expectInstall bool) error {
	deadline := time.Now().Add(3 * time.Minute)
	for time.Now().Before(deadline) {
		if data, err := os.ReadFile(path); err == nil && len(data) > 0 {
			out, decErr := decodeElevatedResult(data)
			if decErr != nil {
				time.Sleep(150 * time.Millisecond)
				continue
			}
			_ = os.Remove(path)
			if out.Msg != "" {
				fmt.Println(out.Msg)
			}
			if !out.OK {
				if out.Err != "" {
					return fmt.Errorf("%s", out.Err)
				}
				return fmt.Errorf("管理员操作失败")
			}
			return nil
		}
		time.Sleep(200 * time.Millisecond)
	}
	info := detectService()
	switch {
	case expectInstall && info.Installed:
		fmt.Println("服务已安装。")
		return nil
	case !expectInstall && !info.Installed:
		fmt.Println("已卸载服务。")
		return nil
	default:
		return fmt.Errorf("等待管理员操作超时")
	}
}
